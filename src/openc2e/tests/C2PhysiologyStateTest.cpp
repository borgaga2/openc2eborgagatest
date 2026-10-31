#include "openc2e/creatures/oldCreature.h"
#include "fileformats/C2SpeechResources.h"
#include "openc2e/creatures/CreatureAgent.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/sfc/C2CreatureData.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace {

class PhysiologyTestAgent : public Agent, public CreatureAgent {
  public:
	PhysiologyTestAgent() : Agent(4, 1, 1, 0) {}
	CompoundPart* part(unsigned int) override { return nullptr; }
	Agent* getAgent() override { return this; }
	unsigned int born_calls = 0, age_calls = 0, death_calls = 0;
  protected:
	void creatureBorn() override { ++born_calls; }
	void creatureAged() override { ++age_calls; }
	void creatureDied() override { ++death_calls; }
};

class TestCreature : public c2Creature {
  public:
	using c2Creature::c2Creature;
	using c2Creature::updateFertilitySignal;
	void ageFrame() { Creature::tick(); }
	unsigned int framePhase() const { return ticks % 4; }
};

std::shared_ptr<genomeFile> make_genome() {
	const std::array<uint8_t, 8> bytes = {'d', 'n', 'a', '2', 'g', 'e', 'n', 'd'};
	SpanReader input(bytes);
	auto genome = std::make_shared<genomeFile>();
	input >> *genome;
	auto lobe = std::make_unique<oldBrainLobeGene>(2);
	lobe->x = lobe->y = 0;
	lobe->width = lobe->height = 1;
	lobe->perceptflag = lobe->nominalthreshold = lobe->leakagerate = 0;
	lobe->reststate = lobe->inputgain = lobe->flags = 0;
	std::fill_n(lobe->staterule, 12, 0);
	const std::array<uint8_t, 256> zeros{};
	SpanReader dendrites(zeros);
	dendrites >> lobe->dendrite1 >> lobe->dendrite2;
	genome->genes.push_back(std::move(lobe));
	return genome;
}

sfc::CreatureV2 saved_state() {
	sfc::CreatureV2 saved;
	saved.sex = 2;
	saved.age = 5;
	saved.age_ticks = 62980;
	saved.biotick = 31491;
	saved.asleep = 1;
	for (size_t i = 0; i < saved.physiological_state.size(); ++i)
		saved.physiological_state[i] = static_cast<uint8_t>(i + 1);
	return saved;
}

} // namespace

TEST(C2PhysiologyState, RestoresAllSavedSignalsAndAgeWithoutLifeEventsOrGeneReplay) {
	PhysiologyTestAgent agent;
	auto genome = make_genome();
	TestCreature creature(genome, true, 0, &agent);
	VectorWriter genome_before;
	genome_before << *genome;
	const auto saved = saved_state();
	creature.restoreC2Physiology(saved);
	EXPECT_EQ(creature.getStage(), old);
	EXPECT_EQ(creature.getAge(), 62980u);
	EXPECT_TRUE(creature.isAsleep());
	EXPECT_TRUE(creature.isAlive());
	EXPECT_EQ(creature.noOrgans(), 0u);
	sfc::CreatureV2 captured;
	creature.updateC2Physiology(captured);
	EXPECT_EQ(captured.physiological_state, saved.physiological_state);
	EXPECT_EQ(captured.age, saved.age);
	EXPECT_EQ(captured.age_ticks, saved.age_ticks);
	EXPECT_EQ(captured.biotick, saved.biotick);
	EXPECT_EQ(captured.sex, saved.sex);
	EXPECT_EQ(captured.life_state, saved.life_state);
	EXPECT_EQ(captured.asleep, saved.asleep);
	EXPECT_EQ(agent.born_calls + agent.age_calls + agent.death_calls, 0u);
	VectorWriter genome_after;
	genome_after << *genome;
	EXPECT_EQ(genome_after.vector(), genome_before.vector());
	creature.ageFrame();
	EXPECT_EQ(creature.getAge(), 62981u);
}

TEST(C2PhysiologyState, BindsIndependentC2ReproductionDeathAndMutationLoci) {
	PhysiologyTestAgent agent;
	TestCreature creature(make_genome(), true, 0, &agent);
	const auto saved = saved_state();
	creature.restoreC2Physiology(saved);
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 2, 0), saved.physiological_state[0]);
	EXPECT_EQ(*creature.getLocusPointer(true, 1, 2, 0), saved.physiological_state[29]);
	EXPECT_NE(creature.getLocusPointer(false, 1, 2, 0), creature.getLocusPointer(true, 1, 2, 0));
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 2, 1), saved.physiological_state[1]);
	EXPECT_EQ(*creature.getLocusPointer(true, 1, 2, 2), saved.physiological_state[31]);
	EXPECT_EQ(*creature.getLocusPointer(true, 1, 2, 3), saved.physiological_state[32]);
	EXPECT_EQ(*creature.getLocusPointer(true, 1, 3, 0), saved.physiological_state[33]);
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 3, 0), 0);
	for (unsigned int i = 0; i < 17; ++i)
		EXPECT_EQ(creature.getDrive(i), saved.physiological_state[66 + i]);
	for (unsigned int i = 0; i < 8; ++i)
		EXPECT_EQ(*creature.getLocusPointer(true, 1, 1, i), saved.physiological_state[21 + i]);
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 4, 12), saved.physiological_state[12]);
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 4, 13), saved.physiological_state[13]);
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 4, 10), 0); // slopes are not in this saved byte block
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 4, 11), 0);
	EXPECT_EQ(creature.getLocusPointer(true, 1, 1, 8), nullptr);
}

TEST(C2PhysiologyState, InvalidSexOrStageLeavesTheLiveControllerUnchanged) {
	PhysiologyTestAgent agent;
	TestCreature creature(make_genome(), true, 0, &agent);
	const auto saved = saved_state();
	creature.restoreC2Physiology(saved);
	for (unsigned int error = 0; error < 3; ++error) {
		auto invalid = saved;
		invalid.age_ticks = 0;
		invalid.physiological_state.fill(0);
		if (error == 0)
			invalid.sex = 0;
		if (error == 1)
			invalid.sex = 1;
		if (error == 2)
			invalid.age = 7;
		EXPECT_THROW(creature.restoreC2Physiology(invalid), Exception);
		sfc::CreatureV2 captured;
		creature.updateC2Physiology(captured);
		EXPECT_EQ(captured.physiological_state, saved.physiological_state);
		EXPECT_EQ(captured.age, saved.age);
		EXPECT_EQ(captured.age_ticks, saved.age_ticks);
		EXPECT_EQ(captured.biotick, saved.biotick);
	}
}

TEST(C2PhysiologyState, DeadCreaturesRemainDeadWithoutReplayingTheDeathAction) {
	PhysiologyTestAgent agent;
	TestCreature creature(make_genome(), true, 0, &agent);
	auto saved = saved_state();
	saved.life_state = 255;
	creature.restoreC2Physiology(saved);
	EXPECT_FALSE(creature.isAlive());
	creature.tick();
	EXPECT_EQ(creature.getAge(), saved.age_ticks);
	EXPECT_EQ(agent.death_calls, 0u);
	sfc::CreatureV2 captured;
	creature.updateC2Physiology(captured);
	EXPECT_EQ(captured.life_state, 255);
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 3, 0), 255);
}

TEST(C2PhysiologyState, PreservesBrainAndBiochemistrySchedulingPhase) {
	PhysiologyTestAgent agent;
	TestCreature creature(make_genome(), true, 0, &agent);
	auto saved = saved_state();
	saved.biotick = 5;
	creature.restoreC2Physiology(saved);
	EXPECT_EQ(creature.framePhase(), 0u); // brain and biochemistry on the next frame
	saved.biotick = 6;
	creature.restoreC2Physiology(saved);
	EXPECT_EQ(creature.framePhase(), 2u); // biochemistry alone on the next frame
}

TEST(C2PhysiologyState, FertilityEmissionUsesTheOriginalC2Hysteresis) {
	PhysiologyTestAgent agent;
	TestCreature creature(make_genome(), true, 0, &agent);
	auto* input = creature.getLocusPointer(true, 1, 2, 0);
	auto* output = creature.getLocusPointer(false, 1, 2, 0);
	*input = 160;
	creature.updateFertilitySignal();
	EXPECT_EQ(*output, 0);
	*input = 161;
	creature.updateFertilitySignal();
	EXPECT_EQ(*output, 255);
	*input = 80;
	creature.updateFertilitySignal();
	EXPECT_EQ(*output, 255);
	*input = 79;
	creature.updateFertilitySignal();
	EXPECT_EQ(*output, 0);
	EXPECT_EQ(*creature.getLocusPointer(true, 1, 2, 2), 127);
	EXPECT_EQ(*creature.getLocusPointer(true, 1, 2, 3), 127);
}

TEST(C2PhysiologyState, RestoresIndividualVocabularyWithoutTeachingUnknownWords) {
	PhysiologyTestAgent agent;
	TestCreature creature(make_genome(), true, 0, &agent);
	EXPECT_TRUE(creature.getC2SpokenWord(0).empty());
	EXPECT_TRUE(creature.getC2SpokenWord(85).empty());
	sfc::CreatureV2 saved;
	for (size_t i = 0; i < saved.vocabulary.size(); ++i) {
		saved.vocabulary[i] = {"input" + std::to_string(i), "output" + std::to_string(i), uint32_t(0x3f000000u + i)};
	}
	saved.vocabulary[12] = {"food", "foo", 0};
	saved.vocabulary[13] = {"unknown", "", 0x3f800000};
	creature.restoreC2Language(saved);
	EXPECT_EQ(creature.getC2SpokenWord(12), "foo");
	EXPECT_TRUE(creature.getC2SpokenWord(13).empty());
	EXPECT_TRUE(creature.getC2SpokenWord(-1).empty());
	EXPECT_TRUE(creature.getC2SpokenWord(86).empty());
	sfc::CreatureV2 captured;
	creature.updateC2Language(captured);
	for (size_t i = 0; i < saved.vocabulary.size(); ++i) {
		EXPECT_EQ(captured.vocabulary[i].in, saved.vocabulary[i].in);
		EXPECT_EQ(captured.vocabulary[i].out, saved.vocabulary[i].out);
		EXPECT_EQ(captured.vocabulary[i].strength, saved.vocabulary[i].strength);
	}
	EXPECT_EQ(agent.born_calls + agent.age_calls + agent.death_calls, 0u);
}

TEST(C2PhysiologyState, ReinforcesExistingWordGraduallyUsingOrderedPronunciationRules) {
	PhysiologyTestAgent agent;
	TestCreature creature(make_genome(), true, 0, &agent);
	sfc::CreatureV2 saved;
	saved.vocabulary[12] = {"thith", "babble", 0};
	creature.restoreC2Language(saved);
	const std::vector<std::pair<std::string, std::string>> rules{{"th", "d"}, {"i", "ee"}};
	EXPECT_FALSE(creature.reinforceC2Word(-1, "thith", rules));
	EXPECT_FALSE(creature.reinforceC2Word(12, "other", rules));
	EXPECT_EQ(creature.getC2SpokenWord(12), "babble");
	ASSERT_TRUE(creature.reinforceC2Word(12, "THITH", rules));
	EXPECT_EQ(creature.getC2SpokenWord(12), "babble"); // strength 60
	ASSERT_TRUE(creature.reinforceC2Word(12, "thith", rules));
	EXPECT_EQ(creature.getC2SpokenWord(12), "dith"); // strength 120
	ASSERT_TRUE(creature.reinforceC2Word(12, "thith", rules));
	EXPECT_EQ(creature.getC2SpokenWord(12), "thith"); // strength 180
	for (int i = 0; i < 5; ++i) creature.reinforceC2Word(12, "thith", rules);
	creature.updateC2Language(saved);
	EXPECT_EQ(saved.vocabulary[12].strength, 255u);
}

TEST(C2PhysiologyState, NewAssociationMatchesOriginalPronunciationObservations) {
	PhysiologyTestAgent agent;
	TestCreature creature(make_genome(), true, 0, &agent);
	for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
		{"food", "gogo"}, {"apple", "gpgp"}, {"push", "gugu"},
		{"computer", "gogo"}, {"ball", "gaga"}, {"eat", "gat"}}) {
		sfc::CreatureV2 saved;
		saved.vocabulary[12] = {"old", "goo", 60};
		creature.restoreC2Language(saved);
		ASSERT_TRUE(creature.learnC2Word(12, pair.first, {}));
		EXPECT_EQ(creature.getC2SpokenWord(12), "goo"); // competing word weakens old association
		ASSERT_TRUE(creature.learnC2Word(12, pair.first, {}));
		EXPECT_EQ(creature.getC2SpokenWord(12), pair.second);
		creature.updateC2Language(saved);
		EXPECT_EQ(saved.vocabulary[12].in, pair.first);
		EXPECT_EQ(saved.vocabulary[12].strength, 60u);
	}
	EXPECT_FALSE(creature.learnC2Word(12, "", {}));
	EXPECT_FALSE(creature.learnC2Word(86, "food", {}));
	EXPECT_FALSE(creature.learnC2Word(12, std::string(25, 'a'), {}));
}

TEST(C2PhysiologyState, InitializesBabbleCategoriesAndLeavesReservedWordsEmpty) {
	PhysiologyTestAgent agent;
	TestCreature creature(make_genome(), true, 0, &agent);
	C2SpeechResources resources;
	for (auto& row : resources.babble) for (unsigned field=0;field<6;++field) row[field]="babble"+std::to_string(field);
	uint32_t seed=0;
	creature.initializeC2Language(resources,seed);
	EXPECT_EQ(seed,1456702548u); // verified native final state after 164 draws
	sfc::CreatureV2 saved; creature.updateC2Language(saved);
	EXPECT_EQ(saved.vocabulary[0].out,"babble0");
	EXPECT_EQ(saved.vocabulary[16].out,"babble5");
	EXPECT_EQ(saved.vocabulary[17].out,"babble1");
	EXPECT_EQ(saved.vocabulary[56].out,"babble2");
	EXPECT_EQ(saved.vocabulary[73].out,"babble3");
	EXPECT_EQ(saved.vocabulary[78].out,"babble4");
	for (unsigned i : {77u,82u,83u,84u,85u}) EXPECT_TRUE(saved.vocabulary[i].out.empty());
	for (const auto& word : saved.vocabulary) if (!word.out.empty()) {
		EXPECT_EQ(word.in,word.out); EXPECT_GE(word.strength,30u); EXPECT_LE(word.strength,50u);
	}
}

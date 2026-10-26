#include "openc2e/creatures/oldBrain.h"
#include "openc2e/creatures/oldCreature.h"
#include "openc2e/creatures/CreatureAgent.h"
#include "common/io/SpanReader.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace {

class InstinctAgent : public Agent, public CreatureAgent {
  public:
	InstinctAgent() : Agent(4, 1, 1, 0) {}
	CompoundPart* part(unsigned int) override { return nullptr; }
	Agent* getAgent() override { return this; }
};

class InstinctCreature : public c2Creature {
  public:
	using c2Creature::c2Creature;
	void queue(creatureInstinctGene& instinct) { unprocessedinstincts.push_back(&instinct); }
	void dreamStep() { tickBrain(); }
	void chemistrySample() { tickBiochemistry(); }
	void setTestStage(lifestage value) { stage = value; }
};

std::shared_ptr<genomeFile> make_genome() {
	const std::array<uint8_t, 8> bytes = {'d', 'n', 'a', '2', 'g', 'e', 'n', 'd'};
	SpanReader input(bytes);
	auto genome = std::make_shared<genomeFile>();
	input >> *genome;
	for (unsigned int i = 0; i < 9; ++i) {
		auto gene = std::make_unique<oldBrainLobeGene>(2);
		gene->x = gene->y = 0;
		gene->width = i == 1 ? 41 : 40;
		gene->height = 1;
		gene->perceptflag = i == 1 || i == 3 || i == 4 ? 1 : 0;
		gene->nominalthreshold = gene->leakagerate = 0;
		gene->reststate = gene->inputgain = gene->flags = 0;
		std::fill_n(gene->staterule, 12, 0);
		const std::array<uint8_t, 256> zeros{};
		SpanReader dendrites(zeros);
		dendrites >> gene->dendrite1 >> gene->dendrite2;
		if (i == 1 || i == 3 || i == 4) {
			gene->dendrite1.srclobe = 2;
			gene->dendrite1.min = gene->dendrite1.max = i == 3 ? 0 : 1;
		}
		if (i == 8)
			gene->dendrite1.min = gene->dendrite1.max = 1;
		if (i == 6) {
			gene->dendrite1.srclobe = gene->dendrite2.srclobe = 8;
			gene->dendrite1.min = gene->dendrite1.max = 1;
			gene->dendrite2.min = gene->dendrite2.max = 1;
		}
		genome->genes.push_back(std::move(gene));
	}
	return genome;
}

creatureInstinctGene make_instinct() {
	creatureInstinctGene instinct(2);
	std::fill_n(instinct.lobes, 3, 0);
	std::fill_n(instinct.neurons, 3, 0);
	instinct.lobes[0] = 2;
	instinct.neurons[0] = 7;
	instinct.action = 0;
	instinct.drive = 52;
	instinct.level = 77;
	return instinct;
}

class C2Instinct : public testing::Test {
  protected:
	InstinctAgent agent;
	InstinctCreature creature{make_genome(), false, 0, &agent};
	oldBrain& brain = *creature.getBrain();
};

} // namespace

TEST_F(C2Instinct, UsesAnEnabledSameSizeRelayAndClearsRewiredConnectionState) {
	auto instinct = make_instinct();
	auto& concept_link = brain.lobes[8]->getNeuron(0)->dendrites[0][0];
	auto& decision_link = brain.lobes[6]->getNeuron(0)->dendrites[0][0];
	concept_link.home_x = concept_link.home_y = concept_link.suscept = 99;
	decision_link.home_x = decision_link.home_y = decision_link.suscept = 88;
	ASSERT_TRUE(brain.processInstinct(instinct));
	// Lobe 1 has the wrong cell count; lobe 3 has its matching input disabled.
	// Lobe 4 starts after the 41 + 40 preceding perceptible cells.
	EXPECT_EQ(concept_link.src, brain.lobes[0]->getNeuron(81 + 7));
	EXPECT_EQ(decision_link.src, brain.lobes[8]->getNeuron(0));
	for (const auto* link : {&concept_link, &decision_link}) {
		EXPECT_EQ(link->stw, 77);
		EXPECT_EQ(link->ltw, 77);
		EXPECT_EQ(link->strength, 77);
		EXPECT_EQ(link->home_x, 0);
		EXPECT_EQ(link->home_y, 0);
		EXPECT_EQ(link->suscept, 0);
	}
}

TEST_F(C2Instinct, FoldsExpressedValuesWithoutChangingTheInstinctGene) {
	auto instinct = make_instinct();
	instinct.lobes[0] = 10;
	instinct.neurons[0] = 255;
	instinct.action = 16;
	ASSERT_TRUE(brain.processInstinct(instinct));
	EXPECT_EQ(brain.lobes[8]->getNeuron(0)->dendrites[0][0].src, brain.lobes[0]->getNeuron(81 + 15));
	EXPECT_EQ(instinct.lobes[0], 10);
	EXPECT_EQ(instinct.neurons[0], 255);
	EXPECT_EQ(instinct.action, 16);
}

TEST_F(C2Instinct, RetainsAnInstinctWhenTheDecisionHasNoAvailableConnection) {
	auto instinct = make_instinct();
	auto& connections = brain.lobes[6]->getNeuron(0)->dendrites[0];
	connections.clear();
	creature.queue(instinct);
	EXPECT_EQ(creature.processInstincts(1), 0u);
	EXPECT_EQ(creature.getNoUnprocessedInstincts(), 1u);
	connections.emplace_back();
	EXPECT_EQ(creature.processInstincts(1), 1u);
	EXPECT_EQ(creature.getNoUnprocessedInstincts(), 0u);
}

TEST_F(C2Instinct, BabiesProcessTheirEntireQueueOneInstinctPerDreamStep) {
	auto instinct = make_instinct();
	creature.queue(instinct);
	creature.queue(instinct);
	creature.startDreaming(1);
	creature.dreamStep();
	EXPECT_TRUE(creature.isDreaming());
	EXPECT_EQ(creature.getNoUnprocessedInstincts(), 1u);
	creature.dreamStep();
	EXPECT_FALSE(creature.isDreaming());
	EXPECT_TRUE(creature.isAsleep());
	EXPECT_EQ(creature.getNoUnprocessedInstincts(), 0u);
}

TEST_F(C2Instinct, OlderCreaturesStopAtTheRequestedInstinctLimit) {
	auto instinct = make_instinct();
	creature.setTestStage(child);
	creature.queue(instinct);
	creature.queue(instinct);
	creature.startDreaming(1);
	creature.dreamStep();
	EXPECT_FALSE(creature.isDreaming());
	EXPECT_TRUE(creature.isAsleep());
	EXPECT_EQ(creature.getNoUnprocessedInstincts(), 1u);
}

TEST_F(C2Instinct, FailedDecisionWiringKeepsDreamingAndItsRemainingLimit) {
	auto instinct = make_instinct();
	creature.setTestStage(child);
	auto& connections = brain.lobes[6]->getNeuron(0)->dendrites[0];
	connections.clear();
	creature.queue(instinct);
	creature.startDreaming(1);
	creature.dreamStep();
	EXPECT_TRUE(creature.isDreaming());
	EXPECT_EQ(creature.getNoUnprocessedInstincts(), 1u);
	connections.emplace_back();
	creature.dreamStep();
	EXPECT_FALSE(creature.isDreaming());
	EXPECT_EQ(creature.getNoUnprocessedInstincts(), 0u);
}

TEST_F(C2Instinct, NormalChemistryDoesNotAdvanceDuringInstinctLearning) {
	auto instinct = make_instinct();
	creature.queue(instinct);
	creature.startDreaming(1);
	const auto before = creature.getBiochemistryTicks();
	creature.chemistrySample();
	EXPECT_EQ(creature.getBiochemistryTicks(), before);
	creature.dreamStep();
	creature.chemistrySample();
	EXPECT_EQ(creature.getBiochemistryTicks(), before + 1);
}

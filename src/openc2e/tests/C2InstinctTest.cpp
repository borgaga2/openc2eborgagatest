#include "openc2e/creatures/oldBrain.h"
#include "openc2e/creatures/C2DendriteCount.h"
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

oldDendriteInfo empty_dendrite_info() {
	oldDendriteInfo info(2);
	const std::array<uint8_t, 128> zeros{};
	SpanReader input(zeros);
	input >> info;
	return info;
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

TEST(C2DendriteCount, AllDrawPairsMatchTheNativeDistribution) {
	auto first = empty_dendrite_info(), second = empty_dendrite_info();
	first.min = 1;
	first.max = 3;
	// Exhaust all nine equally likely pairs. In particular, Saw and waS must
	// both admit two inputs, needed by the original game's paired instincts.
	const std::array<std::array<unsigned int, 3>, 4> expected = {{
		{3, 3, 3}, {3, 5, 1}, {3, 4, 2}, {2, 4, 3}}};
	for (unsigned int spread = 0; spread < 4; ++spread) {
		SCOPED_TRACE(spread);
		first.spread = spread;
		std::array<unsigned int, 3> histogram{};
		for (int a = 0; a <= 2; ++a) {
			for (int b = 0; b <= 2; ++b) {
				unsigned int draws = 0;
				const auto counts = c2::initialDendriteCounts(first, second, [&](int maximum) {
					if (maximum == 0)
						return 0;
					EXPECT_EQ(maximum, 2);
					return draws++ == 0 ? a : b;
				});
				ASSERT_GE(counts[0], 1u);
				ASSERT_LE(counts[0], 3u);
				EXPECT_EQ(counts[1], 0u);
				EXPECT_EQ(draws, spread == 0 ? 1u : 2u);
				++histogram[counts[0] - 1];
			}
		}
		EXPECT_EQ(histogram, expected[spread]);
	}
}

TEST(C2DendriteCount, ZeroMinimaReserveOneInputInEitherEnabledType) {
	auto first = empty_dendrite_info(), second = empty_dendrite_info();
	first.max = second.max = 3;
	for (int chosen = 0; chosen <= 1; ++chosen) {
		unsigned int draws = 0;
		const auto counts = c2::initialDendriteCounts(first, second, [&](int maximum) {
			if (draws++ == 0) {
				EXPECT_EQ(maximum, 1);
				return chosen;
			}
			return 0;
		});
		EXPECT_EQ(counts[0], static_cast<unsigned int>(chosen));
		EXPECT_EQ(counts[1], static_cast<unsigned int>(1 - chosen));
	}
	EXPECT_EQ(first.min, 0);
	EXPECT_EQ(second.min, 0);
}

TEST(C2DendriteCount, DisabledInputsDoNotForceConnectionsOrChangeTheMaximum) {
	auto first = empty_dendrite_info(), second = empty_dendrite_info();
	for (unsigned int enabled = 0; enabled < 2; ++enabled) {
		first.max = enabled == 0 ? 3 : 0;
		second.max = enabled == 1 ? 3 : 0;
		const auto minimum = c2::initialDendriteCounts(first, second, [](int) { return 0; });
		EXPECT_EQ(minimum, (std::array<unsigned int, 2>{0, 0}));
		const auto maximum = c2::initialDendriteCounts(first, second, [](int maximum) { return maximum; });
		EXPECT_EQ(maximum[enabled], 3u);
		EXPECT_EQ(maximum[1 - enabled], 0u);
	}
}

TEST_F(C2Instinct, LobeInitializationHonorsPairedMinimaWithoutEditingTheGene) {
	oldBrainLobeGene gene = *brain.lobes[8]->getGene();
	gene.dendrite1.min = gene.dendrite2.min = 0;
	gene.dendrite1.max = gene.dendrite2.max = 1;
	oldLobe lobe(&brain, &gene);
	lobe.init();
	ASSERT_GT(lobe.getNoNeurons(), 0u);
	for (unsigned int i = 0; i < lobe.getNoNeurons(); ++i) {
		const auto& dendrites = lobe.getNeuron(i)->dendrites;
		EXPECT_LE(dendrites[0].size(), 1u);
		EXPECT_LE(dendrites[1].size(), 1u);
		EXPECT_GE(dendrites[0].size() + dendrites[1].size(), 1u);
	}
	EXPECT_EQ(gene.dendrite1.min, 0);
	EXPECT_EQ(gene.dendrite2.min, 0);
}

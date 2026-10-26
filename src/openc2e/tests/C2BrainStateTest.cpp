#include "openc2e/creatures/oldBrain.h"
#include "openc2e/creatures/oldCreature.h"
#include "openc2e/creatures/CreatureAgent.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"
#include "fileformats/sfc/C2BrainData.h"

#include <cstring>
#include <gtest/gtest.h>

namespace {

class BrainTestAgent : public Agent, public CreatureAgent {
  public:
	BrainTestAgent() : Agent(4, 1, 1, 0) {}
	CompoundPart* part(unsigned int) override { return nullptr; }
	Agent* getAgent() override { return this; }
};

std::shared_ptr<genomeFile> make_genome() {
	const std::array<uint8_t, 8> bytes = {'d', 'n', 'a', '2', 'g', 'e', 'n', 'd'};
	SpanReader input(bytes);
	auto genome = std::make_shared<genomeFile>();
	input >> *genome;
	for (unsigned int i = 0; i < 2; ++i) {
		auto gene = std::make_unique<oldBrainLobeGene>(2);
		gene->x = gene->y = 0; gene->width = gene->height = 1;
		gene->perceptflag = gene->nominalthreshold = gene->leakagerate = 0;
		gene->reststate = gene->inputgain = gene->flags = 0;
		std::fill_n(gene->staterule, 12, 0);
		std::memset(&gene->dendrite1, 0, sizeof(gene->dendrite1));
		std::memset(&gene->dendrite2, 0, sizeof(gene->dendrite2));
		gene->dendrite1.cversion = gene->dendrite2.cversion = 2;
		genome->genes.push_back(std::move(gene));
	}
	return genome;
}

sfc::CBrainV2 make_saved_brain() {
	sfc::CBrainV2 state;
	state.lobes.resize(2);
	state.lobes[0].width = 2; state.lobes[0].height = 1;
	state.lobes[0].num_neurons = 2;
	state.lobes[1].width = state.lobes[1].height = state.lobes[1].num_neurons = 1;
	state.lobes[1].num_dendrites = 1;
	for (auto& lobe : state.lobes) {
		lobe.state[11] = 10; // threshold
		lobe.state[12] = 255; // do not leak on the next nonzero brain tick
		lobe.state[15] = 24; // state rule: random constant
		lobe.state[27] = 101; // saved constant, not the initial zero
		lobe.state[28] = 0xab; // preserve undecoded rule bytes
	}
	state.neurons.resize(3);
	state.neurons[0].state = {0, 0, 55, 65, 1, 2, 3, 4};
	state.neurons[1].state = {1, 0, 45, 55, 0, 0, 0, 0};
	state.neurons[2].state = {0, 0, 35, 45, 0, 0, 0, 0};
	state.neurons[2].dendrites0.push_back({1, 7, 8, 9, 10, 11, 12});
	state.neurons[2].dendrite0_index = 77;
	state.processing_order = {0, 1}; state.ticks = 31415;
	state.reserved_after_neurons = 0x7fffffff;
	return state;
}

std::vector<uint8_t> encode(sfc::CBrainV2 state) {
	VectorWriter bytes;
	MFCWriter writer(bytes);
	sfc::register_c2_brain_class(writer);
	writer(std::make_shared<sfc::CBrainV2>(std::move(state)));
	return bytes.vector();
}

} // namespace

TEST(C2BrainState, RestoresLearnedLinksWithoutTickingOrChangingTheGenome) {
	BrainTestAgent agent;
	auto genome = make_genome();
	c2Creature creature(genome, false, 0, &agent);
	auto& brain = *creature.getBrain();
	auto saved = make_saved_brain();
	brain.restoreC2State(saved);
	EXPECT_EQ(brain.getTicks(), 31415u);
	ASSERT_EQ(brain.lobes[0]->getNoNeurons(), 2u);
	ASSERT_EQ(brain.lobes[1]->getNoNeurons(), 1u);
	auto* neuron = brain.lobes[1]->getNeuron(0);
	ASSERT_EQ(neuron->dendrites[0].size(), 1u);
	EXPECT_EQ(neuron->dendrites[0][0].src, brain.lobes[0]->getNeuron(1));
	EXPECT_EQ(neuron->dendrites[0][0].strength, 12);
	EXPECT_EQ(brain.lobes[0]->getNeuron(0)->leakout, 4);
	EXPECT_EQ(brain.lobes[0]->getNeuron(0)->wta_disable, 1);
	EXPECT_EQ(dynamic_cast<oldBrainLobeGene*>(genome->genes[0].get())->staterule[0], 0);
	auto updated = saved;
	brain.updateC2State(updated);
	EXPECT_EQ(encode(updated), encode(saved));
	neuron->dendrites[0][0].stw = 173;
	brain.updateC2State(updated);
	EXPECT_EQ(updated.neurons[2].dendrites0[0].short_term_weight, 173);
	EXPECT_EQ(updated.lobes[0].state[28], 0xab);
	brain.restoreC2State(saved); // replace a previously restored graph safely
	EXPECT_EQ(brain.lobes[1]->getNeuron(0)->dendrites[0][0].src, brain.lobes[0]->getNeuron(1));
	brain.tick();
	EXPECT_EQ(brain.getTicks(), 31416u);
	EXPECT_EQ(brain.lobes[0]->getNeuron(0)->state, 101);
}

TEST(C2BrainState, InvalidSavedGraphLeavesTheRunningBrainUntouched) {
	BrainTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto& brain = *creature.getBrain();
	auto saved = make_saved_brain();
	brain.restoreC2State(saved);
	const auto original = encode(saved);
	for (unsigned int error = 0; error < 5; ++error) {
		auto invalid = saved;
		if (error == 0) invalid.neurons[2].dendrites0[0].source_index = 2;
		if (error == 1) invalid.processing_order = {0, 0};
		if (error == 2) invalid.lobes[1].num_dendrites = 2;
		if (error == 3) invalid.lobes[0].width = 0;
		if (error == 4) invalid.lobes[1].state[31] = 2;
		EXPECT_THROW(brain.restoreC2State(invalid), Exception);
		auto after = saved;
		brain.updateC2State(after);
		EXPECT_EQ(encode(after), original);
	}
	auto malformed_target = saved;
	malformed_target.neurons.pop_back();
	EXPECT_THROW(brain.updateC2State(malformed_target), Exception);
	EXPECT_EQ(malformed_target.neurons.size(), 2u);
}

TEST(C2BrainState, MigrationConnectsToTheChosenSourceRatherThanItself) {
	BrainTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto saved = make_saved_brain();
	saved.lobes[0].state[15] = 9; // keep each source neuron's existing state
	saved.neurons[1].state[2] = saved.neurons[1].state[3] = 0;
	saved.neurons[2].dendrites0[0].strength = 0;
	saved.lobes[1].state[43] = 1; // migrate a loose dendrite
	for (size_t i = 39; i <= 42; ++i)
		saved.lobes[1].state[i] = 100;
	auto& brain = *creature.getBrain();
	brain.restoreC2State(saved);
	brain.tick();
	auto& dendrite = brain.lobes[1]->getNeuron(0)->dendrites[0][0];
	EXPECT_EQ(dendrite.src, brain.lobes[0]->getNeuron(0));
	EXPECT_EQ(dendrite.strength, 100);
	brain.updateC2State(saved);
	EXPECT_EQ(saved.neurons[2].dendrites0[0].source_index, 0u);
}

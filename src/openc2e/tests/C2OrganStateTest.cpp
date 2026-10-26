#include "openc2e/creatures/oldCreature.h"
#include "openc2e/creatures/CreatureAgent.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"
#include "fileformats/sfc/C2OrganData.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <gtest/gtest.h>

namespace {

class OrganTestAgent : public Agent, public CreatureAgent {
  public:
	OrganTestAgent() : Agent(4, 1, 1, 0) {}
	CompoundPart* part(unsigned int) override { return nullptr; }
	Agent* getAgent() override { return this; }
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
	SpanReader dendrite_input(zeros);
	dendrite_input >> lobe->dendrite1 >> lobe->dendrite2;
	genome->genes.push_back(std::move(lobe));
	return genome;
}

organGene make_organ_gene() {
	organGene gene(2, false);
	gene.clockrate = 1;
	gene.biotickstart = 254;
	gene.damagerate = 0;
	gene.lifeforce = 255;
	gene.atpdamagecoefficient = 24;
	return gene;
}

void put_u32(std::array<uint8_t, 34>& bytes, size_t offset, uint32_t value) {
	for (unsigned int i = 0; i < 4; ++i)
		bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t get_u32(const std::array<uint8_t, 34>& bytes, size_t offset) {
	uint32_t value = 0;
	for (unsigned int i = 0; i < 4; ++i)
		value |= static_cast<uint32_t>(bytes[offset + i]) << (8 * i);
	return value;
}

void put_float(std::array<uint8_t, 34>& bytes, size_t offset, float value) {
	uint32_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	put_u32(bytes, offset, bits);
}

sfc::COrganV2 saved_state() {
	sfc::COrganV2 saved;
	put_u32(saved.state, 0, 17); // organ identity, retained by the caller
	saved.state[4] = 64;
	saved.state[5] = 254;
	put_u32(saved.state, 6, 12345);
	saved.state[10] = 229; // cached health, retained verbatim
	put_float(saved.state, 11, 1000000.0f);
	put_float(saved.state, 15, 900000.0f);
	put_float(saved.state, 19, 950000.0f);
	saved.state[23] = 6;
	saved.state[24] = 32;
	saved.state[25] = 10;
	put_u32(saved.state, 26, 65500);
	put_u32(saved.state, 30, 62745);
	saved.reserved_before_receptors = 1;
	return saved;
}

std::vector<uint8_t> encode(sfc::COrganV2& state) {
	VectorWriter output;
	MFCWriter writer(output);
	sfc::register_c2_organ_classes(writer);
	writer(&state);
	return output.vector();
}

void add_receptor(sfc::COrganV2& state, size_t group, std::array<uint8_t, 8> parameters) {
	if (state.receptor_groups.size() <= group)
		state.receptor_groups.resize(group + 1);
	auto receptor = std::make_shared<sfc::ReceptorV2>();
	receptor->state = parameters;
	state.receptor_groups[group].push_back(receptor);
}

sfc::COrganV2 active_state() {
	auto state = saved_state();
	state.state[4] = 255;
	state.state[5] = 0;
	put_u32(state.state, 6, 0);
	put_float(state.state, 15, 1000000.0f);
	put_float(state.state, 19, 1000000.0f);
	state.state[23] = state.state[24] = state.state[25] = 0;
	put_u32(state.state, 26, 2);
	return state;
}

} // namespace

TEST(C2OrganState, StartsAtTheGenomeClockPhaseAndKeepsIntegerATPInjury) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	organ.processGenes();
	EXPECT_EQ(organ.getATPDamageCoefficient(), 94117u);
	organ.tick(); // phase 254 + 1 activates immediately, with no ATP available
	EXPECT_GT(1000000.0f - organ.getShortTermLifeforce(), 94000.0f);
	EXPECT_LT(1000000.0f - organ.getShortTermLifeforce(), 95000.0f);
	sfc::COrganV2 captured;
	organ.updateC2State(captured);
	EXPECT_EQ(captured.state[5], 0);
	EXPECT_EQ(get_u32(captured.state, 6), 1u);
	gene.biotickstart = gene.clockrate = 255;
	c2Organ full_phase(&creature, &gene);
	full_phase.processGenes();
	full_phase.tick();
	full_phase.updateC2State(captured);
	EXPECT_EQ(captured.state[5], 0);
}

TEST(C2OrganState, RestoresMappedStateWithoutChangingRetainedFields) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	organ.processGenes();
	auto saved = saved_state();
	organ.restoreC2State(saved);
	EXPECT_EQ(organ.getEnergyCost(), 65500u);
	EXPECT_EQ(organ.getATPDamageCoefficient(), 62745u);
	EXPECT_FLOAT_EQ(organ.getShortTermLifeforce(), 900000.0f);
	auto captured = saved;
	organ.updateC2State(captured);
	EXPECT_EQ(captured.state, saved.state);
	EXPECT_EQ(captured.reserved_before_receptors, 1u);
	for (unsigned int error = 0; error < 5; ++error) {
		auto invalid = saved;
		if (error == 0)
			put_float(invalid.state, 11, 0.0f);
		if (error == 1)
			put_float(invalid.state, 15, -1.0f);
		if (error == 2)
			put_float(invalid.state, 19, std::numeric_limits<float>::infinity());
		if (error == 3)
			put_float(invalid.state, 11, std::numeric_limits<float>::quiet_NaN());
		if (error == 4)
			put_float(invalid.state, 19, -1.0f);
		EXPECT_THROW(organ.restoreC2State(invalid), Exception);
		organ.updateC2State(captured);
		EXPECT_EQ(captured.state, saved.state);
	}
}

TEST(C2OrganState, AppliesAnInjuryImpulseOnceAtTheOriginalC2Scale) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	organ.processGenes();
	auto saved = saved_state();
	saved.state[4] = 255;
	saved.state[5] = 0;
	saved.state[23] = saved.state[24] = 0;
	put_float(saved.state, 15, 1000000.0f);
	put_float(saved.state, 19, 1000000.0f);
	put_u32(saved.state, 26, 2);
	organ.restoreC2State(saved);
	creature.addChemical(99, 255);
	organ.tick();
	EXPECT_EQ(organ.getInjuryToApply(), 0);
	EXPECT_NEAR(organ.getShortTermLifeforce(), (1000000.0f - 3921.0f) * 0.99999f, 0.125f);
	const float after_impulse = organ.getShortTermLifeforce();
	organ.tick();
	EXPECT_NEAR(organ.getShortTermLifeforce(), after_impulse * 0.99999f, 0.125f);
}

TEST(C2OrganState, RestoresNetworksAndRunsEmittersThenGroupedReceptorsThenReactions) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	organ.processGenes();
	auto saved = active_state();
	add_receptor(saved, 0, {1, 5, 0, 10, 10, 200, 255, 0});
	add_receptor(saved, 0, {1, 5, 0, 11, 10, 100, 128, 0});
	add_receptor(saved, 0, {1, 5, 0, 12, 0, 0, 200, 1});
	add_receptor(saved, 0, {1, 5, 0, 0, 0, 255, 255, 0}); // chemical zero is ignored
	saved.emitters.push_back({1, 1, 0, 10, 10, 3, 128, 1});
	saved.reactions.push_back({1, 10, 0, 0, 0, 1, 13, 0, 0});
	const auto before = encode(saved);
	organ.restoreC2State(saved);
	// Capture into an empty network, proving the adapter rebuilt parameters.
	auto captured = active_state();
	organ.updateC2State(captured);
	EXPECT_EQ(encode(captured), before);
	EXPECT_EQ(organ.getReceptorCount(), 4u);
	EXPECT_EQ(organ.getEmitterCount(), 1u);
	EXPECT_EQ(organ.getReactionCount(), 1u);
	*creature.getLocusPointer(false, 1, 1, 0) = 200;
	creature.addChemical(10, 15);
	creature.addChemical(11, 110);
	creature.addChemical(12, 50);
	creature.addChemical(99, 255);
	organ.tick();
	EXPECT_EQ(creature.getDrive(0), 135); // mean nominal + mean excitation - mean inhibition
	EXPECT_EQ(creature.getChemical(10), 0);
	EXPECT_EQ(creature.getChemical(13), 110);
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 1, 0), 0);
	EXPECT_EQ(creature.getChemical(99), 253);
	organ.updateC2State(captured);
	EXPECT_EQ(get_u32(captured.state, 6), 1u);
	EXPECT_EQ(captured.reserved_before_receptors, 1u);
}

TEST(C2OrganState, ReceptorsWaitForActivationAndEnergy) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	organ.processGenes();
	auto saved = active_state();
	saved.state[4] = 64;
	add_receptor(saved, 0, {1, 5, 0, 10, 0, 0, 255, 0});
	organ.restoreC2State(saved);
	*creature.getLocusPointer(true, 1, 5, 0) = 77;
	creature.addChemical(10, 200);
	creature.addChemical(99, 255);
	for (unsigned int i = 0; i < 3; ++i)
		organ.tick();
	EXPECT_EQ(creature.getDrive(0), 77);
	EXPECT_FLOAT_EQ(organ.getShortTermLifeforce(), 1000000.0f);
	organ.tick();
	EXPECT_EQ(creature.getDrive(0), 199);
	creature.subChemical(99, 255);
	creature.subChemical(10, 255);
	for (unsigned int i = 0; i < 4; ++i)
		organ.tick();
	EXPECT_EQ(creature.getDrive(0), 199); // no ATP: the network retains its outputs
	EXPECT_EQ(creature.getChemical(48), 0);
	EXPECT_GT(creature.getChemical(31), 0);
}

TEST(C2OrganState, UsesByteSamplingTicksStrictEmitterThresholdAndReactionMaskPhase) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	organ.processGenes();
	auto saved = active_state();
	put_u32(saved.state, 6, 256); // C2 passes the low byte to its network routines
	saved.emitters.push_back({1, 1, 0, 10, 10, 3, 200, 3});
	saved.emitters.push_back({1, 1, 1, 11, 100, 3, 128, 1});
	saved.reactions.push_back({1, 12, 0, 0, 64, 1, 13, 0, 0});
	organ.restoreC2State(saved);
	*creature.getLocusPointer(false, 1, 1, 0) = 10;
	*creature.getLocusPointer(false, 1, 1, 1) = 120;
	creature.addChemical(12, 100);
	creature.addChemical(99, 255);
	organ.tick();
	EXPECT_EQ(creature.getChemical(10), 0); // equal to threshold does not fire or clear
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 1, 0), 10);
	EXPECT_EQ(creature.getChemical(11), 10);
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 1, 1), 0);
	EXPECT_EQ(creature.getChemical(12), 100); // mask one: only odd sample ticks react
	EXPECT_EQ(creature.getChemical(13), 0);
	*creature.getLocusPointer(false, 1, 1, 1) = 200;
	organ.tick();
	EXPECT_EQ(*creature.getLocusPointer(false, 1, 1, 1), 200); // skipped sample retains the source
	EXPECT_EQ(creature.getChemical(11), 10);
	EXPECT_EQ(creature.getChemical(12), 97);
	EXPECT_EQ(creature.getChemical(13), 3);
}

TEST(C2OrganState, EmitsC2InjuryAndRepairSignals) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	organ.processGenes();
	auto saved = active_state();
	put_float(saved.state, 15, 50000.0f);
	organ.restoreC2State(saved);
	organ.applyInjury(100000.0f);
	EXPECT_FLOAT_EQ(organ.getShortTermLifeforce(), 0.0f);
	EXPECT_EQ(creature.getChemical(31), 25); // requested injury, even when life force clamps
	EXPECT_EQ(creature.getChemical(14), 0);
	creature.subChemical(31, 255);
	put_float(saved.state, 15, 900000.0f);
	saved.state[24] = 255;
	organ.restoreC2State(saved);
	creature.addChemical(99, 255);
	organ.tick();
	EXPECT_EQ(creature.getChemical(48), 25);
	EXPECT_EQ(creature.getChemical(31), 0);
	EXPECT_FLOAT_EQ(organ.getShortTermLifeforce(), 999990.0f);
}

TEST(C2OrganState, RejectsInvalidNetworksWithoutReplacingLiveState) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	organ.processGenes();
	auto saved = active_state();
	add_receptor(saved, 0, {1, 5, 0, 10, 0, 0, 255, 0});
	organ.restoreC2State(saved);
	const auto original = encode(saved);
	for (unsigned int error = 0; error < 6; ++error) {
		auto invalid = active_state();
		if (error == 0)
			invalid.receptor_groups = {{nullptr}};
		if (error == 1) {
			add_receptor(invalid, 0, {1, 5, 0, 10, 0, 0, 255, 0});
			add_receptor(invalid, 0, {1, 5, 1, 10, 0, 0, 255, 0});
		}
		if (error == 2)
			invalid.emitters.push_back({1, 1, 0, 10, 0, 0, 128, 0});
		if (error == 3)
			invalid.reactions.push_back({0, 10, 0, 0, 0, 1, 11, 0, 0});
		if (error == 4)
			add_receptor(invalid, 0, {255, 0, 0, 10, 0, 0, 255, 0});
		if (error == 5)
			invalid.emitters.push_back({255, 0, 0, 10, 0, 1, 255, 0});
		EXPECT_THROW(organ.restoreC2State(invalid), Exception);
		auto captured = active_state();
		organ.updateC2State(captured);
		EXPECT_EQ(encode(captured), original);
	}
}

TEST(C2OrganState, PreservesSharedReceptorReferencesWhenCapturingAnEmptyRecord) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	organ.processGenes();
	auto saved = active_state();
	add_receptor(saved, 0, {1, 5, 0, 10, 0, 0, 255, 0x80});
	saved.receptor_groups[0].push_back(saved.receptor_groups[0][0]);
	organ.restoreC2State(saved);
	auto captured = active_state();
	organ.updateC2State(captured);
	EXPECT_EQ(encode(captured), encode(saved));
	ASSERT_EQ(captured.receptor_groups[0].size(), 2u);
	EXPECT_EQ(captured.receptor_groups[0][0], captured.receptor_groups[0][1]);
	EXPECT_NE(captured.receptor_groups[0][0], saved.receptor_groups[0][0]);
}

TEST(C2OrganState, OrganHealthEmitterReadsHealthRatherThanPendingInjury) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	organ.processGenes();
	auto saved = active_state();
	saved.state[10] = 73;
	saved.state[25] = 0;
	saved.emitters.push_back({2, 0, 2, 10, 0, 1, 255, 0});
	organ.restoreC2State(saved);
	creature.addChemical(99, 255);
	organ.tick();
	EXPECT_EQ(creature.getChemical(10), 72);
}

TEST(C2OrganState, CopiesAndNormalizesRuntimeParametersWithoutChangingGenes) {
	OrganTestAgent agent;
	c2Creature creature(make_genome(), false, 0, &agent);
	auto gene = make_organ_gene();
	c2Organ organ(&creature, &gene);
	bioReactionGene reaction(2);
	reaction.quantity[0] = 0;
	reaction.quantity[1] = 17;
	reaction.quantity[2] = 16;
	reaction.quantity[3] = 255;
	std::fill_n(reaction.reactant, 4, 0);
	reaction.rate = 64;
	bioEmitterGene emitter(2);
	emitter.organ = 4;
	emitter.tissue = 1;
	emitter.locus = 0;
	emitter.chemical = 10;
	emitter.threshold = 5;
	emitter.rate = 25;
	emitter.gain = 50;
	emitter.clear = emitter.digital = emitter.invert = false;
	auto replacement = emitter;
	replacement.rate = 0;
	replacement.gain = 200;
	organ.genes = {&reaction, &emitter, &replacement};
	organ.processGenes();
	emitter.gain = replacement.gain = 1; // later DNA changes do not rewrite an expressed network
	sfc::COrganV2 captured;
	organ.updateC2State(captured);
	ASSERT_EQ(captured.reactions.size(), 1u);
	EXPECT_EQ(captured.reactions[0], (std::array<uint8_t, 9>{1, 0, 2, 0, 64, 16, 0, 16, 0}));
	EXPECT_EQ(reaction.quantity[0], 0);
	EXPECT_EQ(reaction.quantity[1], 17);
	ASSERT_EQ(captured.emitters.size(), 1u); // same destination and chemical replace the previous emitter
	EXPECT_EQ(captured.emitters[0], (std::array<uint8_t, 8>{1, 1, 0, 10, 5, 1, 200, 0}));
	EXPECT_EQ(replacement.organ, 4);
	EXPECT_EQ(replacement.rate, 0);
}

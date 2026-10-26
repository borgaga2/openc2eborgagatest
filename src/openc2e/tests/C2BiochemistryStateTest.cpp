#include "openc2e/creatures/oldCreature.h"
#include "openc2e/creatures/CreatureAgent.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"
#include "fileformats/sfc/C2BiochemistryData.h"
#include "fileformats/sfc/C2BrainData.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <gtest/gtest.h>

namespace {

class BiochemistryTestAgent : public Agent, public CreatureAgent {
  public:
	BiochemistryTestAgent() : Agent(4, 1, 1, 0) {}
	CompoundPart* part(unsigned int) override { return nullptr; }
	Agent* getAgent() override { return this; }
};

class TestCreature : public c2Creature {
  public:
	using c2Creature::c2Creature;
	// One biochemistry sample, independent of world-frame scheduling.
	void sample() { tickBiochemistry(); }
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
	auto half_lives = std::make_unique<bioHalfLivesGene>(2);
	std::fill_n(half_lives->halflives, 256, 40);
	genome->genes.push_back(std::move(half_lives));
	return genome;
}

void put_u32(std::array<uint8_t, 34>& state, size_t offset, uint32_t value) {
	for (unsigned int i = 0; i < 4; ++i)
		state[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

void put_float(std::array<uint8_t, 34>& state, size_t offset, float value) {
	uint32_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	put_u32(state, offset, bits);
}

std::shared_ptr<sfc::COrganV2> make_organ(uint32_t identity) {
	auto organ = std::make_shared<sfc::COrganV2>();
	put_u32(organ->state, 0, identity);
	organ->state[4] = 255;
	organ->state[10] = 255;
	put_float(organ->state, 11, 1000000.0f);
	put_float(organ->state, 15, 1000000.0f);
	put_float(organ->state, 19, 1000000.0f);
	put_u32(organ->state, 26, 2);
	put_u32(organ->state, 30, 1000);
	return organ;
}

std::vector<uint8_t> encode(sfc::CBiochemistryV2& state) {
	VectorWriter output;
	MFCWriter writer(output);
	sfc::register_c2_object_classes(writer);
	sfc::register_c2_brain_class(writer);
	sfc::register_c2_biochemistry_classes(writer);
	writer(&state);
	return output.vector();
}

} // namespace

TEST(C2BiochemistryState, PreservesChemicalsSlotsIdentitiesAndSharedArchiveObjects) {
	BiochemistryTestAgent agent;
	TestCreature creature(make_genome(), false, 0, &agent);
	sfc::SimpleObjectV2 owner;
	sfc::CBiochemistryV2 saved;
	saved.owner = &owner;
	for (size_t i = 0; i < saved.chemicals.size(); ++i)
		saved.chemicals[i] = {static_cast<uint8_t>(i), static_cast<uint8_t>(255 - i)};
	auto brain_record = std::make_shared<sfc::CBrainV2>();
	brain_record->state = make_organ(42)->state;
	brain_record->ticks = 77;
	brain_record->lobes.resize(1);
	brain_record->lobes[0].width = brain_record->lobes[0].height = brain_record->lobes[0].num_neurons = 1;
	brain_record->neurons.resize(1);
	brain_record->neurons[0].state[3] = 121;
	brain_record->processing_order = {0};
	saved.organs[0] = brain_record;
	saved.organs[1] = make_organ(17);
	auto receptor = std::make_shared<sfc::ReceptorV2>();
	receptor->state = {1, 5, 0, 10, 0, 0, 255, 0x80};
	saved.organs[0]->receptor_groups = {{receptor}};
	saved.organs[1]->receptor_groups = {{receptor, receptor}};
	saved.organs[127] = saved.organs[1];
	saved.stored_organ_count = 3;
	saved.reserved_after_organs = 112;
	const auto original = encode(saved);
	creature.restoreC2Biochemistry(saved, 12345);
	EXPECT_EQ(creature.getBiochemistryTicks(), 12345u);
	EXPECT_EQ(creature.noOrgans(), 128u);
	EXPECT_FALSE(creature.getOrgan(2));
	EXPECT_EQ(creature.getOrgan(1), creature.getOrgan(127));
	sfc::CBiochemistryV2 captured;
	captured.owner = &owner;
	creature.updateC2Biochemistry(captured);
	EXPECT_EQ(encode(captured), original);
	ASSERT_TRUE(std::dynamic_pointer_cast<sfc::CBrainV2>(captured.organs[0]));
	EXPECT_EQ(std::static_pointer_cast<sfc::CBrainV2>(captured.organs[0])->ticks, 77u);
	EXPECT_NE(captured.organs[0], saved.organs[0]);
	EXPECT_EQ(captured.organs[0]->receptor_groups[0][0], captured.organs[1]->receptor_groups[0][1]);
	EXPECT_EQ(captured.organs[1], captured.organs[127]);
	EXPECT_EQ(encode(saved), original);
}

TEST(C2BiochemistryState, RestoresDecayRatesAndTheirFullCounterPhaseWithoutEditingDNA) {
	BiochemistryTestAgent agent;
	auto genome = make_genome();
	TestCreature creature(genome, false, 0, &agent);
	sfc::CBiochemistryV2 saved;
	saved.chemicals[10] = {100, 64}; // mask 1
	saved.chemicals[11] = {80, 0};
	saved.chemicals[12] = {100, 255};
	creature.restoreC2Biochemistry(saved, 0);
	creature.sample();
	EXPECT_EQ(creature.getChemical(10), 100);
	EXPECT_EQ(creature.getChemical(11), 0);
	EXPECT_EQ(creature.getChemical(12), 100);
	creature.sample();
	EXPECT_EQ(creature.getChemical(10), 97);
	EXPECT_EQ(creature.getBiochemistryTicks(), 2u);
	saved.chemicals[10].decay = 128; // mask 511 uses the full biochemistry counter
	creature.restoreC2Biochemistry(saved, 511);
	creature.sample();
	EXPECT_EQ(creature.getChemical(10), 97);
	EXPECT_EQ(creature.getBiochemistryTicks(), 512u);
	creature.sample();
	EXPECT_EQ(creature.getChemical(10), 97);
	sfc::CBiochemistryV2 captured;
	creature.updateC2Biochemistry(captured);
	EXPECT_EQ(captured.chemicals[10].decay, 128);
	EXPECT_EQ(static_cast<bioHalfLivesGene*>(genome->genes.back().get())->halflives[10], 40);
}

TEST(C2BiochemistryState, RestoredOrgansOperateOnOneLiveChemicalPoolInSlotOrder) {
	BiochemistryTestAgent agent;
	TestCreature creature(make_genome(), false, 0, &agent);
	sfc::CBiochemistryV2 saved;
	for (auto& chemical : saved.chemicals)
		chemical.decay = 255;
	saved.chemicals[10].concentration = 100;
	saved.chemicals[99].concentration = 255;
	saved.organs[0] = make_organ(2);
	saved.organs[0]->reactions.push_back({1, 10, 0, 0, 0, 1, 11, 0, 0});
	saved.organs[1] = make_organ(3);
	auto receptor = std::make_shared<sfc::ReceptorV2>();
	receptor->state = {1, 5, 0, 11, 0, 0, 255, 0};
	saved.organs[1]->receptor_groups = {{receptor}};
	saved.stored_organ_count = 2;
	creature.restoreC2Biochemistry(saved, 0);
	creature.sample();
	EXPECT_EQ(creature.getChemical(10), 0);
	EXPECT_EQ(creature.getChemical(11), 100);
	EXPECT_EQ(creature.getChemical(99), 251);
	EXPECT_EQ(creature.getDrive(0), 99);
}

TEST(C2BiochemistryState, InvalidOrganLeavesTheWholeLiveBiochemistryUntouched) {
	BiochemistryTestAgent agent;
	TestCreature creature(make_genome(), false, 0, &agent);
	sfc::CBiochemistryV2 saved;
	saved.organs[0] = make_organ(7);
	saved.stored_organ_count = 1;
	saved.chemicals[10] = {143, 72};
	creature.restoreC2Biochemistry(saved, 101);
	const auto original = encode(saved);
	for (unsigned int error = 0; error < 2; ++error) {
		auto invalid = saved;
		invalid.chemicals[10].concentration = 0;
		if (error == 0) {
			invalid.organs[1] = make_organ(8);
			put_float(invalid.organs[1]->state, 19, std::numeric_limits<float>::quiet_NaN());
		} else {
			invalid.stored_organ_count = 129;
		}
		EXPECT_THROW(creature.restoreC2Biochemistry(invalid, 999), Exception);
		sfc::CBiochemistryV2 captured;
		creature.updateC2Biochemistry(captured);
		EXPECT_EQ(encode(captured), original);
		EXPECT_EQ(creature.getBiochemistryTicks(), 101u);
	}
}

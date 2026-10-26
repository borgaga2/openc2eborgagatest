#include "openc2e/creatures/oldCreature.h"
#include "openc2e/creatures/CreatureAgent.h"
#include "common/io/SpanReader.h"
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
	std::memset(&lobe->dendrite1, 0, sizeof(lobe->dendrite1));
	std::memset(&lobe->dendrite2, 0, sizeof(lobe->dendrite2));
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

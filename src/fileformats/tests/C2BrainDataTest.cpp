#include "fileformats/sfc/C2BrainData.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"

#include <gtest/gtest.h>

namespace {

std::vector<uint8_t> brain_fixture() {
	std::vector<uint8_t> bytes = {0xff, 0xff, 2, 0, 6, 0, 'C', 'B', 'r', 'a', 'i', 'n'};
	const auto u32 = [&](uint32_t value) {
		for (unsigned int shift = 0; shift < 32; shift += 8)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	};
	for (unsigned int i = 0; i < 54; ++i)
		bytes.push_back(static_cast<uint8_t>(i + 1));
	u32(1); // lobes
	u32(3); u32(4); u32(2); u32(1); // lobe position and dimensions
	for (unsigned int i = 0; i < 247; ++i)
		bytes.push_back(static_cast<uint8_t>(i));
	u32(2); u32(1); // neurons and dendrites
	bytes.insert(bytes.end(), {0, 0, 100, 110, 0, 1, 22, 33});
	bytes.push_back(1); u32(7); // first dendrite set
	u32(1); bytes.insert(bytes.end(), {1, 0, 90, 101, 140, 200});
	bytes.push_back(0); u32(8); // second dendrite set
	bytes.insert(bytes.end(), {1, 0, 120, 130, 1, 0, 44, 55});
	bytes.push_back(0); u32(9);
	bytes.push_back(0); u32(10);
	u32(0x7fffffff); u32(0); u32(15734); // sentinel, processing order, ticks
	return bytes;
}

} // namespace

TEST(C2BrainData, ReadsLearnedNeuronStateAndPreservesOriginalBytes) {
	const auto fixture = brain_fixture();
	ASSERT_EQ(fixture.size(), 399u);
	SpanReader input(fixture);
	MFCReader reader(input);
	sfc::register_c2_brain_class(reader);
	std::shared_ptr<sfc::CBrainV2> brain;
	reader(brain);
	ASSERT_TRUE(brain);
	EXPECT_EQ(input.tell(), fixture.size());
	ASSERT_EQ(brain->neurons.size(), 2u);
	EXPECT_EQ(brain->neurons[0].state[6], 22);
	EXPECT_EQ(brain->neurons[1].state[7], 55);
	ASSERT_EQ(brain->neurons[0].dendrites0.size(), 1u);
	EXPECT_EQ(brain->neurons[0].dendrites0[0].source_index, 1u);
	EXPECT_EQ(brain->neurons[0].dendrites0[0].long_term_weight, 140);
	EXPECT_EQ(brain->neurons[0].dendrites0[0].strength, 200);
	EXPECT_EQ(brain->lobes[0].state[246], 246);
	EXPECT_EQ(brain->ticks, 15734u);
	VectorWriter output;
	MFCWriter writer(output);
	sfc::register_c2_brain_class(writer);
	writer(brain);
	EXPECT_EQ(output.vector(), fixture);
}

TEST(C2BrainData, RejectsTruncatedStateAndInconsistentLobeCounts) {
	auto fixture = brain_fixture();
	fixture.pop_back();
	SpanReader input(fixture);
	MFCReader reader(input);
	sfc::register_c2_brain_class(reader);
	std::shared_ptr<sfc::CBrainV2> brain;
	EXPECT_THROW(reader(brain), std::exception);

	fixture = brain_fixture();
	fixture[337] = 2; // lobe declares two dendrites, while its neurons contain one
	SpanReader inconsistent_input(fixture);
	MFCReader inconsistent_reader(inconsistent_input);
	sfc::register_c2_brain_class(inconsistent_reader);
	EXPECT_THROW(inconsistent_reader(brain), Exception);
}

TEST(C2BrainData, BoundsCountsBeforeAllocatingNeuralState) {
	auto fixture = brain_fixture();
	fixture[66] = 1; fixture[67] = 1; // 257 lobes
	SpanReader input(fixture);
	MFCReader reader(input);
	sfc::register_c2_brain_class(reader);
	std::shared_ptr<sfc::CBrainV2> brain;
	EXPECT_THROW(reader(brain), Exception);
}

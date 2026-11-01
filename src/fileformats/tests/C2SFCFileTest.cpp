#include "fileformats/sfc/C2SFCFile.h"

#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"

#include <gtest/gtest.h>

TEST(C2SFCFile, PreservesSectionsPaddingAndUnknownTail) {
	sfc::C2SFCFile original;
	original.map = std::make_shared<sfc::MapDataV2>();
	original.map_separator = {0, 0, 0, 0, 0};
	for (unsigned int i = 0; i < 256; ++i) {
		auto object = std::make_shared<sfc::SimpleObjectV2>();
		object->unid = i + 1;
		object->variables[99] = i * 3;
		original.objects.push_back(std::move(object));
	}
	auto scenery = std::make_shared<sfc::SceneryV2>();
	scenery->unid = 900;
	original.sceneries.push_back(scenery);
	original.scripts.push_back({2, 3, 7, 4, "outs [C2 world test]"});
	original.script_state.selected_creature = original.objects[0].get();
	original.script_state.favorite_places[2] = {"Cave", -20, 45};
	original.script_state.speech_history = {"hello"};
	auto macro = std::make_shared<sfc::MacroV2>();
	macro->script = "stop";
	macro->owner = original.objects[0].get();
	original.script_state.macros.push_back(macro);
	original.trailing_data = {0, 0xff, 0x43, 0x32};

	VectorWriter encoded;
	sfc::write_c2_sfc_file(encoded, original);
	SpanReader input(encoded.vector());
	auto restored = sfc::read_c2_sfc_file(input);
	ASSERT_EQ(restored.objects.size(), 256u);
	ASSERT_EQ(restored.sceneries.size(), 1u);
	EXPECT_EQ(restored.map_separator, original.map_separator);
	EXPECT_EQ(restored.objects[255]->variables[99], 255u * 3u);
	EXPECT_EQ(restored.script_state.selected_creature, restored.objects[0].get());
	ASSERT_EQ(restored.script_state.macros.size(), 1u);
	EXPECT_EQ(restored.script_state.macros[0]->owner, restored.objects[0].get());
	EXPECT_EQ(restored.trailing_data, original.trailing_data);

	VectorWriter round_trip;
	sfc::write_c2_sfc_file(round_trip, restored);
	EXPECT_EQ(round_trip.vector(), encoded.vector());
}

TEST(C2SFCFile, ClockFollowsVariableLengthObjectListsAndMixedWidthStatistics) {
	sfc::C2SFCFile original;
	original.map = std::make_shared<sfc::MapDataV2>();
	original.map->reserved = {4, 15, 9};
	auto object = std::make_shared<sfc::SimpleObjectV2>();
	object->unid = 23;
	original.objects.push_back(object);
	auto detached = std::make_shared<sfc::SimpleObjectV2>();
	detached->unid = 77;
	original.world_state.discarded_objects = {detached};
	original.world_state.listed_objects = {object, nullptr, object, detached};
	original.world_state.statistics = {101, 202, 303, 404};
	original.world_state.statistics_words = {505, 65535};
	original.world_state.tick = 0xf123abcdu;
	original.world_state.archived_monikers = {"1ABC", "2DEF", "3GHI"};
	original.world_state.state_before_map = {0x81234567, 7};
	original.world_state.map_geometry = {144, 150, 58, 16};
	for (unsigned i = 0; i < original.world_state.state_pairs.size(); ++i)
		original.world_state.state_pairs[i] = {i * 2, 0xffffffffu - i};
	original.world_state.state_after_pairs = {123, 456, 789, 5};
	original.trailing_data = {0xab, 0xcd, 0xef};
	VectorWriter bytes;
	sfc::write_c2_sfc_file(bytes, original);
	SpanReader input(bytes.vector());
	auto decoded = sfc::read_c2_sfc_file(input);
	ASSERT_EQ(decoded.world_state.discarded_objects.size(), 1u);
	ASSERT_EQ(decoded.world_state.listed_objects.size(), 4u);
	EXPECT_EQ(decoded.world_state.discarded_objects[0]->unid, 77u);
	EXPECT_EQ(decoded.world_state.listed_objects[0], decoded.objects[0]);
	EXPECT_EQ(decoded.world_state.listed_objects[1], nullptr);
	EXPECT_EQ(decoded.world_state.listed_objects[2], decoded.objects[0]);
	EXPECT_EQ(decoded.world_state.listed_objects[3], decoded.world_state.discarded_objects[0]);
	EXPECT_EQ(decoded.world_state.statistics, original.world_state.statistics);
	EXPECT_EQ(decoded.world_state.statistics_words, original.world_state.statistics_words);
	EXPECT_EQ(decoded.world_state.tick, original.world_state.tick);
	EXPECT_EQ(decoded.world_state.archived_monikers, original.world_state.archived_monikers);
	EXPECT_EQ(decoded.world_state.state_before_map, original.world_state.state_before_map);
	EXPECT_EQ(decoded.world_state.map_geometry, original.world_state.map_geometry);
	EXPECT_EQ(decoded.world_state.state_pairs, original.world_state.state_pairs);
	EXPECT_EQ(decoded.world_state.state_after_pairs, original.world_state.state_after_pairs);
	EXPECT_EQ(decoded.map->reserved, original.map->reserved);
	EXPECT_EQ(decoded.trailing_data, original.trailing_data);
	VectorWriter round_trip;
	sfc::write_c2_sfc_file(round_trip, decoded);
	EXPECT_EQ(round_trip.vector(), bytes.vector());
}

TEST(C2SFCFile, TruncatedWorldStateIsRejected) {
	sfc::C2SFCFile original;
	original.map = std::make_shared<sfc::MapDataV2>();
	original.objects.push_back(std::make_shared<sfc::SimpleObjectV2>());
	VectorWriter bytes;
	sfc::write_c2_sfc_file(bytes, original);
	auto truncated = bytes.vector();
	truncated.pop_back();
	SpanReader input(truncated);
	EXPECT_THROW(sfc::read_c2_sfc_file(input), Exception);
}

TEST(C2SFCFile, ExcessiveArchivedMonikerCountIsRejectedBeforeReadingItsRecords) {
	// Empty reference lists, zero statistics and tick, then an invalid count.
	std::array<uint32_t, 9> prefix{};
	prefix.back() = 17;
	VectorWriter bytes;
	MFCWriter writer(bytes);
	for (auto& value : prefix) writer(value);
	SpanReader input(bytes.vector());
	MFCReader reader(input);
	sfc::WorldStateV2 decoded;
	try {
		decoded.serialize(reader);
		FAIL() << "Accepted more monikers than the native world's sixteen slots";
	} catch (const Exception& error) {
		EXPECT_EQ(std::string(error.what()), "C2 world has too many archived monikers");
	}
	EXPECT_TRUE(decoded.archived_monikers.empty());
	sfc::WorldStateV2 oversized;
	oversized.archived_monikers.assign(17, "1ABC");
	VectorWriter output;
	MFCWriter invalid_writer(output);
	EXPECT_THROW(oversized.serialize(invalid_writer), Exception);
}

TEST(C2SFCFile, RejectsArchiveWithoutC2ObjectSectionMarker) {
	sfc::C2SFCFile original;
	original.map = std::make_shared<sfc::MapDataV2>();
	original.objects.push_back(std::make_shared<sfc::SimpleObjectV2>());
	VectorWriter encoded;
	sfc::write_c2_sfc_file(encoded, original);
	VectorWriter map_bytes;
	MFCWriter map_writer(map_bytes);
	sfc::register_c2_map_classes(map_writer);
	map_writer(original.map);
	auto truncated = encoded.vector();
	truncated[map_bytes.vector().size() + original.map_separator.size() + sizeof(uint32_t)] = 0;
	SpanReader input(truncated);
	EXPECT_ANY_THROW(sfc::read_c2_sfc_file(input));
}


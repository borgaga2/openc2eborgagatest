#include "fileformats/sfc/C2MapData.h"

#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"

#include <gtest/gtest.h>

static std::vector<uint8_t> write_map(std::shared_ptr<sfc::MapDataV2> map) {
	VectorWriter out;
	MFCWriter writer(out);
	sfc::register_c2_map_classes(writer);
	writer(map);
	return out.vector();
}

TEST(SFCMap, C2RoundTripPreservesRoomStateAndObjectReferences) {
	auto map = std::make_shared<sfc::MapDataV2>();
	map->reserved = {17, 18, 19};
	map->background = std::make_shared<sfc::CGalleryV1>();
	map->background->filename = "Back";
	map->background->absolute_base = 0;
	map->background->refcount = 1;
	map->background->images.push_back({map->background.get(), 3, 144, 150, 16});
	auto first = std::make_shared<sfc::RoomV2>();
	first->id = 1;
	first->left = 100;
	first->right = 300;
	first->top = 200;
	first->bottom = 400;
	first->temperature = 99;
	first->heat_source = -255;
	first->pressure_source = 255;
	first->light_source = 128;
	first->radiation_source = -129;
	first->wind_x = -1000;
	first->wind_y = 1000;
	first->reserved_environment[799] = 42;
	first->floor_points = {{0, 190}, {200, 180}};
	first->music = "caf\xc3\xa9";
	first->reserved = 123;
	first->drop_status = 1;
	auto door = std::make_shared<sfc::DoorV2>();
	door->openness = 80;
	door->other_room = 3;
	first->doors[0].push_back(door);
	first->doors[1].push_back(door);
	auto second = std::make_shared<sfc::RoomV2>();
	second->id = 3;
	map->rooms = {nullptr, first, nullptr, second};

	auto bytes = write_map(map);
	SpanReader in(bytes);
	MFCReader reader(in);
	sfc::register_c2_map_classes(reader);
	std::shared_ptr<sfc::MapDataV2> restored;
	reader(restored);
	ASSERT_NE(restored, nullptr);
	ASSERT_EQ(restored->rooms.size(), 4u);
	EXPECT_EQ(restored->rooms[0], nullptr);
	EXPECT_EQ(restored->rooms[2], nullptr);
	ASSERT_NE(restored->rooms[1], nullptr);
	const auto& room = *restored->rooms[1];
	EXPECT_EQ(room.heat_source, -255);
	EXPECT_EQ(room.pressure_source, 255);
	EXPECT_EQ(room.light_source, 128);
	EXPECT_EQ(room.radiation_source, -129);
	EXPECT_EQ(room.wind_x, -1000);
	EXPECT_EQ(room.wind_y, 1000);
	EXPECT_EQ(room.music, first->music);
	EXPECT_EQ(room.reserved_environment, first->reserved_environment);
	EXPECT_EQ(room.floor_points, first->floor_points);
	EXPECT_EQ(restored->reserved, map->reserved);
	ASSERT_EQ(room.doors[0].size(), 1u);
	ASSERT_EQ(room.doors[1].size(), 1u);
	EXPECT_EQ(room.doors[0][0], room.doors[1][0]);
	ASSERT_EQ(restored->background->images.size(), 1u);
	EXPECT_EQ(restored->background->images[0].parent, restored->background.get());
	EXPECT_FALSE(in.has_data_left());
	EXPECT_EQ(write_map(restored), bytes);
}

TEST(SFCMap, C2RejectsTruncatedArchive) {
	auto map = std::make_shared<sfc::MapDataV2>();
	map->rooms.push_back(std::make_shared<sfc::RoomV2>());
	auto bytes = write_map(map);
	bytes.pop_back();
	SpanReader in(bytes);
	MFCReader reader(in);
	sfc::register_c2_map_classes(reader);
	std::shared_ptr<sfc::MapDataV2> restored;
	EXPECT_ANY_THROW(reader(restored));
}

TEST(SFCMap, C2RetainsReferencesForFollowingWorldObjects) {
	auto map = std::make_shared<sfc::MapDataV2>();
	map->background = std::make_shared<sfc::CGalleryV1>();
	map->background->filename = "Back";
	map->background->absolute_base = 0;
	map->background->refcount = 1;
	map->rooms = {std::make_shared<sfc::RoomV2>(), nullptr};
	VectorWriter out;
	MFCWriter writer(out);
	sfc::register_c2_map_classes(writer);
	writer(map);
	writer(map->background);
	writer(map->rooms[0]);

	SpanReader in(out.vector());
	MFCReader reader(in);
	sfc::register_c2_map_classes(reader);
	std::shared_ptr<sfc::MapDataV2> restored;
	std::shared_ptr<sfc::CGalleryV1> gallery;
	std::shared_ptr<sfc::RoomV2> room;
	reader(restored);
	reader(gallery);
	reader(room);
	ASSERT_NE(restored, nullptr);
	ASSERT_EQ(restored->rooms.size(), 1u);
	EXPECT_EQ(gallery, restored->background);
	EXPECT_EQ(room, restored->rooms[0]);
	EXPECT_FALSE(in.has_data_left());
}

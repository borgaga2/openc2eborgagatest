#pragma once

#include "CGallery.h"
#include "common/NumericCast.h"
#include "fileformats/MFCObject.h"
#include "fileformats/MFCReader.h"

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace sfc {

struct DoorV2 : MFCObject {
	uint8_t openness = 0;
	uint16_t other_room = 0;
	uint16_t reserved = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(openness);
		ar(other_room);
		ar(reserved);
	}
};

struct RoomV2 : MFCObject {
	uint32_t id = 0;
	uint16_t room_format = 2;
	int32_t left = 0, top = 0, right = 0, bottom = 0;
	std::array<std::vector<std::shared_ptr<DoorV2>>, 4> doors;
	int32_t type = 0;
	uint8_t floor_value = 0, inorganic_nutrients = 0, organic_nutrients = 0;
	uint8_t temperature = 0, pressure = 0, light = 0, radiation = 0;
	int32_t heat_source = 0, pressure_source = 0, light_source = 0, radiation_source = 0;
	int32_t wind_x = 0, wind_y = 0;
	std::array<uint8_t, 800> reserved_environment{};
	std::vector<std::pair<uint32_t, uint32_t>> floor_points;
	uint32_t reserved = 0;
	std::string music;
	uint32_t drop_status = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(id);
		ar(room_format);
		if (room_format != 2)
			throw Exception("Unsupported C2 room format");
		ar(left);
		ar(top);
		ar(right);
		ar(bottom);
		for (auto& direction : doors) {
			ar.size_u16(direction);
			for (auto& door : direction)
				ar(door);
		}
		ar(type);
		ar(floor_value);
		ar(inorganic_nutrients);
		ar(organic_nutrients);
		ar(temperature);
		ar(heat_source);
		ar(pressure);
		ar(pressure_source);
		ar(wind_x);
		ar(wind_y);
		ar(light);
		ar(light_source);
		ar(radiation);
		ar(radiation_source);
		ar(std::span<uint8_t>(reserved_environment));
		ar.size_u16(floor_points);
		for (auto& point : floor_points) {
			ar(point.first);
			ar(point.second);
		}
		ar(reserved);
		ar.win1252_mfcstring(music);
		ar(drop_status);
	}
};

struct MapDataV2 : MFCObject {
	uint32_t version = 1;
	std::array<uint32_t, 3> reserved{};
	std::shared_ptr<CGalleryV1> background;
	// The archive's count excludes deleted (null) rooms, but its object stream
	// includes them. Keep those slots so writing preserves IDs and references.
	std::vector<std::shared_ptr<RoomV2>> rooms;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(version);
		if (version != 1)
			throw Exception("Expected a Creatures 2 map");
		for (auto& value : reserved)
			ar(value);
		ar(background);
		uint32_t live_rooms = numeric_cast<uint32_t>(std::count_if(rooms.begin(), rooms.end(), [](const auto& room) { return room != nullptr; }));
		ar(live_rooms);
		if constexpr (std::is_same_v<Archive, MFCReader>) {
			rooms.clear();
			while (live_rooms != 0) {
				std::shared_ptr<RoomV2> room;
				ar(room);
				if (room)
					--live_rooms;
				rooms.push_back(std::move(room));
			}
		} else {
			for (auto& room : rooms) {
				if (live_rooms == 0)
					break;
				ar(room);
				if (room)
					--live_rooms;
			}
		}
	}
};

// Keep these registrations on the caller's archive: later world objects can
// refer back to the map's galleries and other MFC objects.
// Rooms and doors were introduced with schema 1; C2's map/gallery use schema 2.
template <typename Archive>
void register_c2_map_classes(Archive& ar) {
	ar.template register_class<MapDataV2>("MapData", 2);
	ar.template register_class<CGalleryV1>("CGallery", 2);
	ar.template register_class<RoomV2>("CRoom", 1);
	ar.template register_class<DoorV2>("CDoor", 1);
}

} // namespace sfc

#pragma once

#include "C2ObjectData.h"

namespace sfc {

// The native world serializer writes these immediately after the macro table.
// Read both reference lists using the same MFC context as the world objects;
// their variable-length records make a fixed tail offset incorrect.
struct WorldStateV2 {
	// The native loader disposes of objects in the first list. Retain the
	// second list for round trips; its runtime behavior is not restored here.
	std::vector<std::shared_ptr<ObjectV2>> discarded_objects, listed_objects;
	std::array<uint32_t, 4> statistics{};
	std::array<uint16_t, 2> statistics_words{};
	uint32_t tick = 0;
	// Native world +0xd8 has space for sixteen four-byte monikers; +0x118
	// is its count. Keep these identities even when no living creature uses them.
	std::vector<std::string> archived_monikers;
	// Retain unmapped values without assigning them invented runtime meanings.
	std::array<uint32_t, 2> state_before_map{};
	// Globals +0x4d1d18/+0x4d1d1c/+0x4d1d14/+0x4d1d10, in archive order.
	std::array<uint32_t, 4> map_geometry{144, 150, 58, 16};
	// Twenty inline native objects at world +0x174 (runtime stride 12), each
	// serialized as two DWORDs by the original 0x49c450 routine.
	std::array<std::array<uint32_t, 2>, 20> state_pairs{};
	std::array<uint32_t, 4> state_after_pairs{};

	template <typename Archive>
	void serialize(Archive& ar) {
		for (auto* objects : {&discarded_objects, &listed_objects}) {
			ar.size_u32(*objects);
			for (auto& object : *objects) ar(object);
		}
		for (auto& value : statistics) ar(value);
		for (auto& value : statistics_words) ar(value);
		ar(tick);
		uint32_t count = numeric_cast<uint32_t>(archived_monikers.size());
		ar(count);
		if (count > 16) throw Exception("C2 world has too many archived monikers");
		if constexpr (std::is_same_v<Archive, MFCReader>) archived_monikers.resize(count);
		for (auto& moniker : archived_monikers) ar.ascii_dword(moniker);
		for (auto& value : state_before_map) ar(value);
		for (auto& value : map_geometry) ar(value);
		for (auto& pair : state_pairs)
			for (auto& value : pair) ar(value);
		for (auto& value : state_after_pairs) ar(value);
	}
};

} // namespace sfc

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

	template <typename Archive>
	void serialize(Archive& ar) {
		for (auto* objects : {&discarded_objects, &listed_objects}) {
			ar.size_u32(*objects);
			for (auto& object : *objects) ar(object);
		}
		for (auto& value : statistics) ar(value);
		for (auto& value : statistics_words) ar(value);
		ar(tick);
	}
};

} // namespace sfc

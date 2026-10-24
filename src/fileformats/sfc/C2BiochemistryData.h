#pragma once

#include "C2ObjectData.h"
#include "C2OrganData.h"

namespace sfc {

struct CBiochemistryV2 : MFCObject {
	ObjectV2* owner = nullptr;
	struct Chemical {
		uint8_t concentration = 0, decay = 0;
	};
	std::array<Chemical, 256> chemicals{};
	std::array<std::shared_ptr<COrganV2>, 128> organs{};
	uint32_t stored_organ_count = 0;
	// Retain the two state bytes preceding the Creature's sex/age fields.
	uint16_t reserved_after_organs = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(owner);
		for (auto& chemical : chemicals) {
			ar(chemical.concentration);
			ar(chemical.decay);
		}
		for (auto& organ : organs)
			ar(organ);
		ar(stored_organ_count);
		if (stored_organ_count > organs.size())
			throw Exception("C2 biochemistry has too many organs");
		ar(reserved_after_organs);
	}
};

template <typename Archive>
void register_c2_biochemistry_classes(Archive& ar) {
	register_c2_organ_classes(ar);
	ar.template register_class<CBiochemistryV2>("CBiochemistry", 2);
}

} // namespace sfc

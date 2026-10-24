#pragma once

#include "C2ObjectData.h"

namespace sfc {

struct BodyPartV2 : EntityV2 {
	int32_t angle = 0, view = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		EntityV2::serialize(ar);
		ar(angle);
		ar(view);
	}
};

struct BodyV2 : BodyPartV2 {
	// Six attachment points, each stored across the ten body views.
	std::array<std::array<std::array<int8_t, 2>, 10>, 6> attachments{};

	template <typename Archive>
	void serialize(Archive& ar) {
		BodyPartV2::serialize(ar);
		for (auto& point : attachments) {
			for (auto& view : point) {
				ar(view[0]);
				ar(view[1]);
			}
		}
	}
};

struct LimbV2 : BodyPartV2 {
	std::array<std::array<uint8_t, 4>, 10> attachments{};
	std::shared_ptr<LimbV2> next;

	template <typename Archive>
	void serialize(Archive& ar) {
		BodyPartV2::serialize(ar);
		for (auto& view : attachments)
			ar(std::span<uint8_t>(view));
		ar(next);
	}
};

template <typename Archive>
void register_c2_body_classes(Archive& ar) {
	// The body classes retain schema 1 in C2, while their inherited entity
	// animation buffer uses C2's 99-byte representation.
	ar.template register_class<BodyV2>("Body", 1);
	ar.template register_class<LimbV2>("Limb", 1);
}

} // namespace sfc

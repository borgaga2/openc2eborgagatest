#pragma once

#include "common/Exception.h"
#include "common/NumericCast.h"
#include "fileformats/MFCObject.h"
#include "fileformats/MFCReader.h"

#include <array>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace sfc {

struct ReceptorV2 : MFCObject {
	std::array<uint8_t, 8> state{};

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(std::span<uint8_t>(state));
	}
};

struct COrganV2 : MFCObject {
	// Includes the organ's identity, life forces and timing. Preserve the
	// original representation until all fields have been decoded.
	std::array<uint8_t, 34> state{};
	uint32_t reserved_before_receptors = 1;
	std::vector<std::vector<std::shared_ptr<ReceptorV2>>> receptor_groups;
	std::vector<std::array<uint8_t, 8>> emitters;
	std::vector<std::array<uint8_t, 9>> reactions;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(std::span<uint8_t>(state));
		ar(reserved_before_receptors);
		size_t total_receptors = 0;
		for (const auto& group : receptor_groups)
			total_receptors += group.size();
		uint32_t declared_receptors = numeric_cast<uint32_t>(total_receptors);
		ar(declared_receptors);
		if (declared_receptors > 65536)
			throw Exception("C2 organ has too many receptors");
		uint32_t num_groups = numeric_cast<uint32_t>(receptor_groups.size());
		ar(num_groups);
		if (num_groups > 65536)
			throw Exception("C2 organ has too many receptor groups");
		if constexpr (std::is_same_v<Archive, MFCReader>)
			receptor_groups.resize(num_groups);
		total_receptors = 0;
		for (auto& group : receptor_groups) {
			ar.size_u16(group);
			total_receptors += group.size();
			if (total_receptors > declared_receptors)
				throw Exception("C2 organ receptor groups exceed their declared count");
			for (auto& receptor : group)
				ar(receptor);
		}
		if (total_receptors != declared_receptors)
			throw Exception("C2 organ receptor groups do not match their declared count");
		serialize_entries(ar, emitters);
		serialize_entries(ar, reactions);
	}

  private:
	template <typename Archive, size_t N>
	static void serialize_entries(Archive& ar, std::vector<std::array<uint8_t, N>>& entries) {
		uint32_t count = numeric_cast<uint32_t>(entries.size());
		ar(count);
		if (count > 65536)
			throw Exception("C2 organ has too many biochemical entries");
		if constexpr (std::is_same_v<Archive, MFCReader>)
			entries.resize(count);
		for (auto& entry : entries)
			ar(std::span<uint8_t>(entry));
	}
};

template <typename Archive>
void register_c2_organ_classes(Archive& ar) {
	ar.template register_class<COrganV2>("COrgan", 1);
	ar.template register_class<ReceptorV2>("CReceptor", 1);
}

} // namespace sfc

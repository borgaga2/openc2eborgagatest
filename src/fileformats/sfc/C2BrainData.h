#pragma once

#include "common/Exception.h"
#include "common/NumericCast.h"
#include "C2OrganData.h"
#include "fileformats/MFCObject.h"
#include "fileformats/MFCReader.h"

#include <array>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace sfc {

struct DendriteV2 {
	uint32_t source_index = 0;
	uint8_t home_x = 0, home_y = 0;
	uint8_t susceptibility = 0, short_term_weight = 0, long_term_weight = 0, strength = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(source_index);
		ar(home_x);
		ar(home_y);
		ar(susceptibility);
		ar(short_term_weight);
		ar(long_term_weight);
		ar(strength);
	}
};

struct NeuronV2 {
	// C2 adds two state bytes to C1's six-byte neuron header. Retain every
	// byte, including flags and per-neuron state, when saving a learned brain.
	std::array<uint8_t, 8> state{};
	uint32_t dendrite0_index = 0, dendrite1_index = 0;
	std::vector<DendriteV2> dendrites0, dendrites1;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(std::span<uint8_t>(state));
		ar.size_u8(dendrites0);
		ar(dendrite0_index);
		for (auto& dendrite : dendrites0)
			dendrite.serialize(ar);
		ar.size_u8(dendrites1);
		ar(dendrite1_index);
		for (auto& dendrite : dendrites1)
			dendrite.serialize(ar);
	}
};

struct LobeV2 {
	uint32_t x = 0, y = 0, width = 0, height = 0;
	// This contains the lobe parameters and C2's six rules per dendrite
	// type. Keep their original representation until all fields are decoded.
	std::array<uint8_t, 247> state{};
	uint32_t num_neurons = 0, num_dendrites = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(x);
		ar(y);
		ar(width);
		ar(height);
		ar(std::span<uint8_t>(state));
		ar(num_neurons);
		ar(num_dendrites);
	}
};

struct CBrainV2 : COrganV2 {
	std::vector<LobeV2> lobes;
	std::vector<NeuronV2> neurons;
	uint32_t reserved_after_neurons = 0;
	std::vector<uint32_t> processing_order;
	uint32_t ticks = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		COrganV2::serialize(ar);
		uint32_t num_lobes = numeric_cast<uint32_t>(lobes.size());
		ar(num_lobes);
		if (num_lobes > 256)
			throw Exception("C2 brain has too many lobes");
		if constexpr (std::is_same_v<Archive, MFCReader>)
			lobes.resize(num_lobes);

		size_t total_neurons = 0;
		for (auto& lobe : lobes) {
			lobe.serialize(ar);
			if (lobe.num_neurons > 65536 || total_neurons > 1048576 - lobe.num_neurons)
				throw Exception("C2 brain has too many neurons");
			total_neurons += lobe.num_neurons;
		}
		if constexpr (std::is_same_v<Archive, MFCReader>) {
			neurons.resize(total_neurons);
			processing_order.resize(num_lobes);
		} else {
			if (neurons.size() != total_neurons || processing_order.size() != num_lobes)
				throw Exception("C2 brain state does not match its lobe table");
		}

		size_t index = 0;
		for (const auto& lobe : lobes) {
			size_t dendrites = 0;
			for (uint32_t i = 0; i < lobe.num_neurons; ++i) {
				auto& neuron = neurons[index++];
				neuron.serialize(ar);
				dendrites += neuron.dendrites0.size() + neuron.dendrites1.size();
			}
			if (dendrites != lobe.num_dendrites)
				throw Exception("C2 brain dendrites do not match its lobe table");
		}
		ar(reserved_after_neurons);
		for (auto& lobe : processing_order)
			ar(lobe);
		ar(ticks);
	}
};

template <typename Archive>
void register_c2_brain_class(Archive& ar) {
	register_c2_organ_classes(ar);
	ar.template register_class<CBrainV2>("CBrain", 2);
}

} // namespace sfc

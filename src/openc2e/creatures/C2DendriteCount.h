#pragma once

#include "fileformats/genomeFile.h"

#include <array>
#include <cstdlib>

namespace c2 {

// random(max) supplies an independent integer in [0, max] on each call.
// Inputs are expressed dendrite parameters, with max >= min.
template <typename Random>
std::array<unsigned int, 2> initialDendriteCounts(
	const oldDendriteInfo& first, const oldDendriteInfo& second, Random&& random) {
	const bool paired = first.min == 0 && second.min == 0 && first.max > 0 && second.max > 0;
	int bias = paired ? random(1) : 0;
	std::array<unsigned int, 2> counts{};
	const oldDendriteInfo* inputs[] = {&first, &second};
	for (unsigned int type = 0; type < 2; ++type) {
		const auto& info = *inputs[type];
		const int minimum = info.min + bias;
		const int range = info.max - minimum;
		int value = 0;
		if (info.spread <= 3) {
			value = random(range);
			if (info.spread != 0) {
				// C2 draws twice; doubling the first draw loses intermediate counts.
				value += random(range);
				if (info.spread == 1)
					value /= 2;
				else {
					value = std::abs(value - range);
					if (info.spread == 3)
						value = range - value;
				}
			}
		}
		counts[type] = minimum + value;
		if (paired)
			bias = 1 - bias;
	}
	return counts;
}

} // namespace c2

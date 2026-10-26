#include "oldCreature.h"
#include "common/Exception.h"
#include "fileformats/sfc/C2OrganData.h"

#include <cmath>
#include <cstring>
#include <fmt/core.h>
#include <unordered_map>

namespace {

uint32_t read_u32(const std::array<uint8_t, 34>& bytes, size_t offset) {
	uint32_t value = 0;
	for (unsigned int i = 0; i < 4; ++i)
		value |= static_cast<uint32_t>(bytes[offset + i]) << (8 * i);
	return value;
}

float read_float(const std::array<uint8_t, 34>& bytes, size_t offset) {
	const uint32_t bits = read_u32(bytes, offset);
	float value;
	static_assert(sizeof(value) == sizeof(bits));
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

void write_u32(std::array<uint8_t, 34>& bytes, size_t offset, uint32_t value) {
	for (unsigned int i = 0; i < 4; ++i)
		bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

void write_float(std::array<uint8_t, 34>& bytes, size_t offset, float value) {
	uint32_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	write_u32(bytes, offset, bits);
}

} // namespace

void c2Organ::restoreC2State(const sfc::COrganV2& saved) {
	const float initial = read_float(saved.state, 11);
	const float short_term = read_float(saved.state, 15);
	const float long_term = read_float(saved.state, 19);
	if (!std::isfinite(initial) || !std::isfinite(short_term) || !std::isfinite(long_term) ||
		initial <= 0.0f || short_term < 0.0f || long_term < 0.0f)
		throw Exception("Saved C2 organ has invalid life force");
	if (saved.receptor_groups.size() > 65536 || saved.emitters.size() > 65536 || saved.reactions.size() > 65536)
		throw Exception("Saved C2 organ has too many biochemical entries");

	// These are runtime parameters, not reconstructed genes. Prepare and bind
	// the network before replacing any live state; the actual genome is kept.
	std::vector<c2Receptor> restored_receptors;
	std::vector<std::vector<size_t>> restored_groups;
	std::vector<size_t> restored_identities;
	std::unordered_map<const sfc::ReceptorV2*, size_t> identities;
	std::vector<c2Emitter> restored_emitters;
	std::vector<std::shared_ptr<c2Reaction>> restored_reactions;
	for (const auto& group : saved.receptor_groups) {
		auto& indices = restored_groups.emplace_back();
		for (const auto& receptor : group) {
			if (!receptor || restored_receptors.size() >= 65536)
				throw Exception("Saved C2 organ has invalid receptors");
			restored_receptors.emplace_back(receptor->state, this);
			const auto& restored = restored_receptors.back();
			if (!restored.locus)
				throw Exception(fmt::format("Cannot bind saved C2 receptor locus ({}, {}, {})",
					restored.organ, restored.tissue, restored.locus_id));
			if (!indices.empty()) {
				const auto& first = restored_receptors[indices.front()];
				if (restored.organ != first.organ || restored.tissue != first.tissue || restored.locus_id != first.locus_id)
					throw Exception("Saved C2 receptor group has different destinations");
			}
			auto identity = identities.try_emplace(receptor.get(), restored_receptors.size() - 1).first;
			restored_identities.push_back(identity->second);
			indices.push_back(restored_receptors.size() - 1);
		}
	}
	for (const auto& emitter : saved.emitters) {
		if (emitter[5] == 0)
			throw Exception("Saved C2 emitter has a zero sampling rate");
		restored_emitters.emplace_back(emitter, this);
		if (!restored_emitters.back().locus)
			throw Exception(fmt::format("Cannot bind saved C2 emitter locus ({}, {}, {})", emitter[0], emitter[1], emitter[2]));
	}
	for (const auto& reaction : saved.reactions) {
		if ((reaction[1] && !reaction[0]) || (reaction[3] && !reaction[2]))
			throw Exception("Saved C2 reaction has a zero input quantity");
		restored_reactions.push_back(std::make_shared<c2Reaction>(reaction));
	}
	receptors.swap(restored_receptors);
	receptor_groups.swap(restored_groups);
	receptor_identities.swap(restored_identities);
	emitters.swap(restored_emitters);
	reactions.swap(restored_reactions);
	lifeforce = initial;
	shorttermlifeforce = short_term;
	longtermlifeforce = long_term;
	clockrate = saved.state[4];
	biotick = saved.state[5];
	biochemticks = read_u32(saved.state, 6);
	cached_health = saved.state[10];
	energy_gate = saved.reserved_before_receptors;
	damagerate = saved.state[23];
	repairrate = saved.state[24];
	injurytoapply = saved.state[25];
	energycost = read_u32(saved.state, 26);
	atpdamagecoefficient = read_u32(saved.state, 30);
}

void c2Organ::updateC2State(sfc::COrganV2& saved) const {
	write_float(saved.state, 11, lifeforce);
	write_float(saved.state, 15, shorttermlifeforce);
	write_float(saved.state, 19, longtermlifeforce);
	saved.state[4] = clockrate;
	saved.state[5] = numeric_cast<uint8_t>(biotick);
	write_u32(saved.state, 6, biochemticks);
	saved.state[10] = cached_health;
	saved.reserved_before_receptors = energy_gate;
	saved.state[23] = damagerate;
	saved.state[24] = repairrate;
	saved.state[25] = injurytoapply;
	write_u32(saved.state, 26, energycost);
	write_u32(saved.state, 30, atpdamagecoefficient);
	saved.receptor_groups.resize(receptor_groups.size());
	std::unordered_map<size_t, std::shared_ptr<sfc::ReceptorV2>> captured_receptors;
	for (size_t i = 0; i < receptor_groups.size(); ++i) {
		auto& group = saved.receptor_groups[i];
		group.resize(receptor_groups[i].size());
		for (size_t j = 0; j < group.size(); ++j) {
			const size_t index = receptor_groups[i][j];
			auto& captured = captured_receptors[receptor_identities[index]];
			if (!captured) {
				captured = std::make_shared<sfc::ReceptorV2>();
				captured->state = receptors[index].parameters();
			}
			group[j] = captured;
		}
	}
	saved.emitters.resize(emitters.size());
	for (size_t i = 0; i < emitters.size(); ++i)
		saved.emitters[i] = emitters[i].parameters();
	saved.reactions.resize(reactions.size());
	for (size_t i = 0; i < reactions.size(); ++i)
		saved.reactions[i] = reactions[i]->parameters();
}

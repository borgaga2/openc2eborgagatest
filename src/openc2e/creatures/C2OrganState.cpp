#include "oldCreature.h"
#include "common/Exception.h"
#include "fileformats/sfc/C2OrganData.h"

#include <cmath>
#include <cstring>

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
	// Only mapped mutable values are applied here. The organ identity, cached
	// health byte, receptor groups and other retained fields remain in the file.
	const float initial = read_float(saved.state, 11);
	const float short_term = read_float(saved.state, 15);
	const float long_term = read_float(saved.state, 19);
	if (!std::isfinite(initial) || !std::isfinite(short_term) || !std::isfinite(long_term) ||
		initial <= 0.0f || short_term < 0.0f || long_term < 0.0f)
		throw Exception("Saved C2 organ has invalid life force");
	lifeforce = initial;
	shorttermlifeforce = short_term;
	longtermlifeforce = long_term;
	clockrate = saved.state[4];
	biotick = saved.state[5];
	biochemticks = read_u32(saved.state, 6);
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
	saved.state[23] = damagerate;
	saved.state[24] = repairrate;
	saved.state[25] = injurytoapply;
	write_u32(saved.state, 26, energycost);
	write_u32(saved.state, 30, atpdamagecoefficient);
}

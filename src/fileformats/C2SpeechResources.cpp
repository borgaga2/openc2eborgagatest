#include "C2SpeechResources.h"
#include "common/Exception.h"
#include "common/encoding.h"
#include <algorithm>

C2SpeechResources readC2SpeechResources(std::span<const uint8_t> data) {
	auto fail = [] { throw Exception("Unsupported or truncated C2 speech executable"); };
	auto range = [&](size_t p, size_t n) { if (p > data.size() || n > data.size() - p) fail(); };
	auto u16 = [&](size_t p) { range(p, 2); return uint32_t(data[p]) | (uint32_t(data[p+1]) << 8); };
	auto u32 = [&](size_t p) { range(p, 4); uint32_t v = 0; for (unsigned i=0;i<4;++i) v |= uint32_t(data[p+i]) << (8*i); return v; };
	if (u16(0) != 0x5a4d) fail();
	const size_t pe = u32(0x3c);
	if (u32(pe) != 0x4550 || u16(pe+4) != 0x14c) fail();
	const auto count = u16(pe+6), optional_size = u16(pe+20);
	const size_t optional = pe+24;
	if (optional_size < 32 || u16(optional) != 0x10b || u32(optional+28) != 0x400000) fail();
	const size_t sections = optional + optional_size;
	range(sections, size_t(count)*40);
	auto address = [&](uint32_t va, size_t n) {
		if (va < 0x400000) fail();
		const uint32_t rva = va - 0x400000;
		for (unsigned i=0;i<count;++i) {
			const size_t p = sections + i*40;
			const auto start = u32(p+12), raw_size = u32(p+16), raw = u32(p+20);
			if (rva < start || rva-start > raw_size || n > raw_size-(rva-start)) continue;
			const size_t out = size_t(raw) + (rva-start);
			range(out, n); return out;
		}
		fail(); return size_t(0);
	};
	// Guard the code that indexes the verified table, before using its VAs.
	const std::array<uint8_t, 6> signature{0x51,0x56,0x8b,0x74,0x24,0x0c};
	const auto code = address(0x4221c0, signature.size());
	if (u32(address(0x41fe0b,4)) != 0x4b2220 ||
		u32(address(0x4205aa,4)) != 0x4cd700) fail();
	if (!std::equal(signature.begin(),signature.end(),data.begin()+code)) fail();
	auto text = [&](uint32_t va, size_t capacity) {
		const auto p = address(va, capacity);
		size_t n = 0; while (n < capacity && data[p+n]) ++n;
		if (n == capacity) fail();
		return cp1252_to_utf8(std::string(reinterpret_cast<const char*>(data.data()+p), n));
	};
	C2SpeechResources result;
	for (unsigned row=0;row<12;++row) for (unsigned field=0;field<6;++field) {
		const auto pointer = u32(address(0x4b2220+row*24+field*4,4));
		result.babble[row][field] = text(pointer,25);
		if (result.babble[row][field].empty()) fail();
	}
	for (unsigned i=0;i<18;++i) {
		auto from=text(0x4cd700+i*8,4), to=text(0x4cd704+i*8,4);
		if (from.empty()) fail();
		result.pronunciation_rules.emplace_back(std::move(from), std::move(to));
	}
	return result;
}

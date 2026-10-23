#include "C2EggFile.h"

#include "common/Exception.h"
#include "common/endianlove.h"
#include "common/io/SpanReader.h"
#include "fileformats/genomeFile.h"

#include <algorithm>
#include <array>
#include <cstring>

C2EggFile C2EggFile::read(Reader& input) {
	// See the Creatures Development Network's EGG file format specification:
	// https://lisdude.com/cdn/89.html
	auto bytes = input.read_to_end();
	SpanReader reader(bytes);
	std::array<char, 4> magic;
	reader.read(magic.data(), magic.size());
	if (std::memcmp(magic.data(), "eggf", 4) != 0)
		throw Exception("Invalid C2 egg file signature");
	C2EggFile egg;
	egg.gender = read8(reader);
	egg.image = read8(reader);
	if (egg.gender > 2 || egg.image > 16)
		throw Exception("Invalid C2 egg gender or image");
	const auto reserved1 = read32le(reader);
	const auto reserved2 = read32le(reader);
	if (reserved1 != 0 || reserved2 != 0)
		throw Exception("Unsupported C2 egg reserved fields");
	egg.mother_moniker.resize(4);
	egg.father_moniker.resize(4);
	reader.read(egg.mother_moniker.data(), 4);
	reader.read(egg.father_moniker.data(), 4);
	const bool has_father = std::any_of(egg.father_moniker.begin(), egg.father_moniker.end(), [](char c) { return c != 0; });
	if (!has_father)
		egg.father_moniker.clear();
	auto read_blob = [&]() {
		const auto size = read32le(reader);
		if (size > bytes.size() - reader.tell())
			throw Exception("Truncated C2 egg field");
		std::vector<uint8_t> result(size);
		if (size)
			reader.read(result.data(), result.size());
		return result;
	};
	const auto description = read_blob();
	egg.description.assign(description.begin(), description.end());
	egg.mother_genome = read_blob();
	if (has_father)
		egg.father_genome = read_blob();
	if (reader.has_data_left())
		throw Exception("Unexpected data after C2 egg genomes");
	auto validate_genome = [](const std::vector<uint8_t>& data) {
		SpanReader genome_reader(data);
		genomeFile genome;
		genome_reader >> genome;
		if (genome.getVersion() != 2)
			throw Exception("Invalid C2 egg parent genome version");
	};
	validate_genome(egg.mother_genome);
	if (has_father)
		validate_genome(egg.father_genome);
	return egg;
}

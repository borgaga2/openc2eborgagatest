#include "fileformats/C2EggFile.h"

#include "common/endianlove.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"

#include <gtest/gtest.h>

static std::vector<uint8_t> make_egg(bool two_parents) {
	VectorWriter writer;
	writer.write("eggf");
	write8(writer, 2);
	write8(writer, 16);
	write32le(writer, 0);
	write32le(writer, 0);
	writer.write("mum1");
	if (two_parents)
		writer.write("dad1");
	else
		write32le(writer, 0);
	write32le(writer, 4);
	writer.write("test");
	write32le(writer, 8);
	writer.write("dna2gend");
	if (two_parents) {
		write32le(writer, 8);
		writer.write("dna2gend");
	}
	return writer.vector();
}

TEST(C2EggFile, ReadsSingleParent) {
	auto bytes = make_egg(false);
	SpanReader reader(bytes);
	const auto egg = C2EggFile::read(reader);
	EXPECT_EQ(egg.gender, 2);
	EXPECT_EQ(egg.image, 16);
	EXPECT_EQ(egg.mother_moniker, "mum1");
	EXPECT_TRUE(egg.father_moniker.empty());
	EXPECT_EQ(egg.description, "test");
	EXPECT_EQ(egg.mother_genome.size(), 8u);
	EXPECT_TRUE(egg.father_genome.empty());
}

TEST(C2EggFile, ReadsTwoParents) {
	auto bytes = make_egg(true);
	SpanReader reader(bytes);
	const auto egg = C2EggFile::read(reader);
	EXPECT_EQ(egg.father_moniker, "dad1");
	EXPECT_EQ(egg.father_genome, egg.mother_genome);
}

TEST(C2EggFile, RejectsEveryTruncatedPrefix) {
	const auto bytes = make_egg(true);
	for (size_t size = 0; size < bytes.size(); ++size) {
		SpanReader reader(std::span<const uint8_t>(bytes.data(), size));
		EXPECT_THROW(C2EggFile::read(reader), std::exception) << "prefix length " << size;
	}
}

TEST(C2EggFile, RejectsInvalidHeaderAndLengths) {
	const auto valid = make_egg(false);
	for (size_t position : {size_t(0), size_t(4), size_t(5), size_t(6), size_t(10), size_t(22), size_t(30)}) {
		auto invalid = valid;
		invalid[position] = 255;
		SpanReader reader(invalid);
		EXPECT_THROW(C2EggFile::read(reader), std::exception) << "byte " << position;
	}
}

TEST(C2EggFile, RejectsBadGenomesAndTrailingData) {
	auto bytes = make_egg(false);
	bytes.back() = '!';
	SpanReader bad_genome(bytes);
	EXPECT_THROW(C2EggFile::read(bad_genome), std::exception);
	bytes = make_egg(false);
	bytes.push_back(0);
	SpanReader trailing(bytes);
	EXPECT_THROW(C2EggFile::read(trailing), std::exception);
}

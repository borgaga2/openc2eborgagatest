#include "openc2e/SFCFile.h"

#include "common/endianlove.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"

#include <array>
#include <gtest/gtest.h>

TEST(SFCFile, C2RoomPreservesSignedEnvironmentSources) {
	VectorWriter out;
	// An MFC CRoom object, with the C2 room layout from the world archive.
	write16le(out, 0xffff);
	write16le(out, 1);
	write16le(out, 5);
	out.write("CRoom", 5);
	write32le(out, 16); // room ID
	write16le(out, 2); // room format
	for (uint32_t coordinate : {100u, 200u, 300u, 400u})
		write32le(out, coordinate);
	for (int direction = 0; direction < 4; ++direction)
		write16le(out, 0); // no doors
	write32le(out, 2); // room type
	for (uint8_t value : {5, 6, 7, 8})
		write8(out, value); // floor, nutrients, temperature
	writesigned32le(out, -255); // heat source
	write8(out, 9); // pressure
	writesigned32le(out, 255); // pressure source
	writesigned32le(out, -1234); // wind X
	writesigned32le(out, 5678); // wind Y
	write8(out, 10); // light
	writesigned32le(out, 128); // light source
	write8(out, 11); // radiation
	writesigned32le(out, -129); // radiation source
	std::array<char, 800> reserved{};
	out.write(reserved.data(), reserved.size());
	write16le(out, 2); // floor points
	for (uint32_t coordinate : {0u, 190u, 200u, 180u})
		write32le(out, coordinate);
	write32le(out, 0x12345678); // opaque room state need not be zero
	write8(out, 4); // length-prefixed room music
	out.write("test", 4);
	write32le(out, 1); // drop status

	SpanReader in(out.vector());
	SFCFile archive(in);
	auto* room = dynamic_cast<CRoom*>(archive.slurpMFC());
	ASSERT_NE(room, nullptr);
	EXPECT_EQ(room->heatsource, -255);
	EXPECT_EQ(room->pressuresource, 255);
	EXPECT_EQ(room->lightsource, 128);
	EXPECT_EQ(room->radiationsource, -129);
	EXPECT_EQ(room->windx, -1234);
	EXPECT_EQ(room->windy, 5678);
	EXPECT_EQ(room->temperature, 8);
	EXPECT_EQ(room->pressure, 9);
	EXPECT_EQ(room->lightlevel, 10);
	EXPECT_EQ(room->radiation, 11);
	ASSERT_EQ(room->floorpoints.size(), 2u);
	EXPECT_EQ(room->floorpoints[1].first, 200u);
	EXPECT_EQ(room->floorpoints[1].second, 180u);
	EXPECT_EQ(room->reserved_after_floorpoints, 0x12345678u);
	EXPECT_EQ(room->music, "test");
	EXPECT_EQ(room->dropstatus, 1u);
	EXPECT_FALSE(in.has_data_left());
}

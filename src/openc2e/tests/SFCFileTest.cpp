#include "openc2e/SFCFile.h"
#include "openc2e/Agent.h"
#include "openc2e/Engine.h"
#include "common/Exception.h"
#include <algorithm>
#include <memory>

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

TEST(SFCFile, RestoresC2TimerCountdownAndPreservesC1Conversion) {
	struct Environment {
		int version = engine.version;
		std::string gametype = engine.gametype;
		~Environment() { engine.version = version; engine.gametype = gametype; }
	} environment;
	engine.version = 2; engine.gametype = "c2";
	class TimerAgent : public Agent {
	  public:
		TimerAgent() : Agent(2, 1, 1, 1000) {}
		CompoundPart* part(unsigned int) override { return nullptr; }
		void physicsTick() override {}
	};
	auto agent = std::make_shared<TimerAgent>();
	SFCFile archive;
	archive.setVersion(1);
	SFCSimpleObject saved(&archive);
	saved.tickreset = 8;
	for (unsigned int remaining : {8u, 5u, 1u, 0u}) {
		saved.tickstate = remaining;
		saved.copyTimerToAgent(*agent);
		const auto delay = std::max(1u, remaining);
		EXPECT_EQ(agent->getTicksUntilTimer(), delay);
		for (unsigned int tick = 1; tick < delay; ++tick) {
			agent->tick();
			EXPECT_EQ(agent->getTicksUntilTimer(), delay - tick);
		}
		agent->tick();
		EXPECT_EQ(agent->getTicksUntilTimer(), 8u);
	}
	saved.tickreset = saved.tickstate = 0;
	saved.copyTimerToAgent(*agent);
	agent->tick();
	EXPECT_EQ(agent->getTicksUntilTimer(), 0u);
	saved.tickreset = 8; saved.tickstate = 9;
	EXPECT_THROW(saved.copyTimerToAgent(*agent), Exception);
	EXPECT_EQ(agent->timerrate, 0u);
	engine.version = 1; engine.gametype = "c1";
	SFCFile c1archive;
	c1archive.setVersion(0);
	SFCSimpleObject c1saved(&c1archive);
	c1saved.tickreset = 8; c1saved.tickstate = 5;
	c1saved.copyTimerToAgent(*agent);
	EXPECT_EQ(agent->getTicksUntilTimer(), 3u);
	c1saved.tickstate = 8;
	c1saved.copyTimerToAgent(*agent);
	EXPECT_EQ(agent->getTicksUntilTimer(), 8u);
}

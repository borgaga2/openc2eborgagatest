#include "openc2e/CompoundPart.h"
#include "openc2e/Map.h"
#include "openc2e/MetaRoom.h"
#include "openc2e/C2HearingRange.h"
#include "openc2e/Agent.h"
#include "openc2e/Engine.h"
#include "openc2e/World.h"
#include <gtest/gtest.h>

TEST(C2HearingRange, NativeAxisBoundariesAndHorizontalWrap) {
	EXPECT_TRUE(c2WithinHearingRange(100,100,100,1000));
	EXPECT_FALSE(c2WithinHearingRange(101,0,100,1000));
	EXPECT_FALSE(c2WithinHearingRange(0,101,100,1000));
	EXPECT_FALSE(c2WithinHearingRange(890,0,100,1000));
	EXPECT_TRUE(c2WithinHearingRange(900,0,100,1000));
	EXPECT_TRUE(c2WithinHearingRange(-980,0,100,1000));
	EXPECT_FALSE(c2WithinHearingRange(400,0,100,1000));
}

TEST(C2HearingRange, FloorSideMatchesNativeStrictAndHalfOpenBoundaries) {
	Room room(0,200,0,0,200,200);
	room.floorpoints = {{0,100},{200,100}};
	EXPECT_TRUE(c2AboveFloor(room,50,99));
	EXPECT_FALSE(c2AboveFloor(room,50,100));
	EXPECT_FALSE(c2AboveFloor(room,50,101));
	EXPECT_TRUE(c2AboveFloor(room,199,50));
	EXPECT_FALSE(c2AboveFloor(room,200,50));
	room.floorpoints.clear();
	EXPECT_FALSE(c2AboveFloor(room,50,50));
}

TEST(C2HearingRange, CrossRoomDoorsAndWrappedBoundaries) {
	class EarPart : public CompoundPart {
	  public: EarPart(Agent* a) : CompoundPart(a,0,0,0,0) {}
		void partRender(RenderTarget*,int,int) override {}
		unsigned int getWidth() override {return 0;}
		unsigned int getHeight() override {return 0;}
	};
	class Ear : public Agent {
		EarPart body;
	  public: Ear() : Agent(2,1,1,0),body(this) {} CompoundPart* part(unsigned int) override {return &body;}
	};
	const auto version=engine.version;engine.version=2;
	auto old_map=std::move(world.map);world.map=std::make_unique<Map>();
	auto* meta=world.map->addMetaRoom(0,0,1000,400,"",true);
	auto a=std::make_shared<Room>(0,500,0,0,200,200), b=std::make_shared<Room>(500,1000,0,0,200,200);
	a->floorvalue=b->floorvalue=255;meta->addRoom(a);meta->addRoom(b);
	auto ear=std::make_shared<Ear>(),source=std::make_shared<Ear>();
	ear->x=450;ear->y=100;ear->size=127;ear->range=200;source->x=550;source->y=100;
	EXPECT_FALSE(c2CanHear(ear.get(),source.get()));
	world.map->setDoorPerm(a,b,126);EXPECT_FALSE(c2CanHear(ear.get(),source.get()));
	world.map->setDoorPerm(a,b,127);EXPECT_TRUE(c2CanHear(ear.get(),source.get()));
	ear->x=950;source->x=50;EXPECT_TRUE(c2CanHear(ear.get(),source.get()));
	world.map->setDoorPerm(a,b,0);EXPECT_FALSE(c2CanHear(ear.get(),source.get()));
	// A downward room boundary also checks the source room's floor value.
	auto below=std::make_shared<Room>(0,1000,200,200,400,400);
	below->floorvalue=255;meta->addRoom(below);world.map->setDoorPerm(a,below,255);
	ear->x=450;ear->y=100;ear->range=400;source->x=450;source->y=300;
	a->floorvalue=0;EXPECT_FALSE(c2CanHear(ear.get(),source.get()));
	a->floorvalue=255;EXPECT_TRUE(c2CanHear(ear.get(),source.get()));
	world.map=std::move(old_map);engine.version=version;
}

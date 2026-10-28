#include "openc2e/creatures/SkeletalCreature.h"
#include "openc2e/Engine.h"
#include "openc2e/Map.h"
#include "openc2e/World.h"
#include "openc2e/caosScript.h"
#include "openc2e/caosVM.h"
#include "common/Exception.h"
#include "common/creaturesImage.h"
#include "fileformats/sfc/C2CreatureData.h"

#include <gtest/gtest.h>
#include <limits>

namespace {
class BodyAgent : public SkeletalCreature {
  public:
	BodyAgent() : SkeletalCreature(4) {}
	const creaturesImage& image(unsigned int i) const { return *images[i]; }
	const attFile& attachments(unsigned int i) const { return att[i]; }
	std::string poseText(unsigned int i) const { const auto* p = posegenes.at(i)->pose; return std::string(p, p + 15); }
	unsigned int gaitStep(unsigned int i, unsigned int step) const { return gaitgenes.at(i)->pose[step]; }
	unsigned int facing() const { return posedirection; }
	bool hasTail() const { return c2_has_tail; }
};

class C2SkeletonState : public testing::Test {
  protected:
	int previous_version = engine.version;
	std::unique_ptr<Map> previous_map;
	void SetUp() override {
		engine.version = 2;
		previous_map = std::move(world.map);
		world.map = std::make_unique<Map>();
		world.map->addMetaRoom(0, 0, 2000, 1000, "", true);
	}
	void TearDown() override {
		world.map = std::move(previous_map);
		engine.version = previous_version;
	}
	std::shared_ptr<creaturesImage> gallery() {
		auto image = std::make_shared<creaturesImage>("test");
		for (unsigned int i = 0; i < 250; ++i) {
			Image frame;
			frame.width = frame.height = 1;
			frame.format = if_rgb565;
			frame.data = shared_array<uint8_t>(2);
			frame.data[0] = i;
			frame.data[1] = 0;
			image->images.push_back(frame);
		}
		return image;
	}
	sfc::CreatureV2 saved() {
		sfc::CreatureV2 s;
		s.direction = 2;
		s.current_pose = "252212212011232";
		s.expression = 1;
		s.eyes_open = 1;
		s.downfoot_right = 0;
		s.body = std::make_shared<sfc::BodyV2>();
		s.body->gallery = std::make_shared<sfc::CGalleryV1>();
		s.body->gallery->filename = "test";
		s.body->base = 120;
		s.body->view = 2;
		s.body->x = 500;
		s.body->y = 300;
		s.body->attachments[0][2] = {-3, 12};
		const unsigned int counts[] = {1, 3, 3, 2, 2, 2};
		unsigned int next_base = 130;
		for (unsigned int chain = 0; chain < 6; ++chain) {
			auto* next = &s.limbs[chain];
			for (unsigned int index = 0; index < counts[chain]; ++index) {
				*next = std::make_shared<sfc::LimbV2>();
				auto& limb = **next;
				limb.gallery = s.body->gallery;
				limb.base = chain == 0 ? 0 : next_base;
				if (chain != 0) next_base += 10;
				limb.view = chain == 0 ? 9 : 2;
				limb.pose_plus_base = limb.base + limb.view + (chain == 0 ? 20 : 0);
				limb.x = 480 + chain;
				limb.y = 290 + index * 15;
				limb.attachments[limb.view] = {1, 2, 5, 7};
				next = &limb.next;
			}
		}
		s.poses[17] = "202222222222222";
		s.gait_animations[3] = "017018019020021022023024R";
		return s;
	}
};
}

TEST_F(C2SkeletonState, RestoresCompositeSlicesAttachmentsAndSavedAnimationTables) {
	BodyAgent agent;
	auto s = saved();
	auto sprite = gallery();
	unsigned int loads = 0;
	agent.restoreC2Body(s, [&](const auto&) { ++loads; return sprite; });
	EXPECT_EQ(loads, 1u);
	EXPECT_EQ(agent.image(1).numframes(), 120u);
	EXPECT_EQ(agent.image(0).numframes(), 10u);
	EXPECT_EQ(agent.image(1).data(29), sprite->data(29));
	EXPECT_EQ(agent.image(11).data(2), sprite->data(152));
	EXPECT_EQ(agent.image(12).data(2), sprite->data(182));
	EXPECT_EQ(agent.image(13).data(2), sprite->data(242));
	EXPECT_EQ(agent.getPose(1), 9u);
	EXPECT_EQ(agent.getFacialExpression(), 1u);
	EXPECT_FALSE(agent.getEyesClosed());
	EXPECT_TRUE(agent.isLeftFootDown());
	EXPECT_EQ(agent.x + agent.attachmentX(11, 1), 486);
	EXPECT_EQ(agent.y + agent.attachmentY(11, 1), 327);
	EXPECT_EQ(static_cast<int>(agent.attachments(0).attachments[2][0]), -3);
	EXPECT_EQ(agent.poseText(17), s.poses[17]);
	EXPECT_EQ(agent.gaitStep(3, 7), 24u);
	EXPECT_EQ(agent.facing(), 0u);
}

TEST_F(C2SkeletonState, C2AnimCommandPlaysTripletPosesAndRepeatsIncludingPose255) {
	auto agent = std::make_shared<BodyAgent>();
	auto s = saved();
	s.poses[17] = "222222222222222";
	s.poses[255] = "233333333333333";
	agent->restoreC2Body(s, [&](const auto&) { return gallery(); });
	auto* part = dynamic_cast<AnimatablePart*>(agent->part(0));
	caosScript script("c2", "animation-test");
	script.parse("anim [017255R]");
	caosVM vm(agent.get());
	vm.runEntirely(script.installer);
	EXPECT_EQ(agent->getPose(1), 9u); // ANIM does not apply a pose immediately.
	part->tick();
	EXPECT_EQ(agent->getPose(1), 2u);
	part->tick();
	EXPECT_EQ(agent->getPose(1), 3u);
	part->tick();
	EXPECT_EQ(agent->getPose(1), 2u);
	EXPECT_EQ(part->getFrameNo(), 1u);
}

TEST_F(C2SkeletonState, FiniteC2AnimationReleasesOverAfterItsLastPose) {
	auto agent = std::make_shared<BodyAgent>();
	auto s = saved();
	s.poses[18] = "233333333333333";
	agent->restoreC2Body(s, [&](const auto&) { return gallery(); });
	auto* part = dynamic_cast<AnimatablePart*>(agent->part(0));
	caosScript script("c2", "animation-test");
	script.parse("anim [017018],over,setv ov00 7,endm");
	caosVM vm(agent.get());
	ASSERT_TRUE(vm.fireScript(script.installer, false));
	vm.tick(); // ANIM
	vm.timeslice = 1;
	vm.tick(); // OVER blocks
	EXPECT_TRUE(vm.isBlocking());
	part->tick();
	EXPECT_TRUE(vm.isBlocking());
	EXPECT_EQ(agent->var[0].getInt(), 0);
	part->tick();
	EXPECT_FALSE(vm.isBlocking());
	for (unsigned i = 0; i < 4 && !vm.stopped(); ++i) {
		vm.timeslice = 1;
		vm.tick();
	}
	EXPECT_EQ(agent->var[0].getInt(), 7);
	EXPECT_EQ(agent->getPose(1), 3u);
	part->tick();
	EXPECT_EQ(agent->getPose(1), 3u);
}

TEST_F(C2SkeletonState, InvalidC2AnimationPreservesSequenceAndCursor) {
	BodyAgent agent;
	agent.restoreC2Body(saved(), [&](const auto&) { return gallery(); });
	agent.setC2Animation("017017R");
	auto* part = dynamic_cast<AnimatablePart*>(agent.part(0));
	part->tick();
	const auto animation = part->animation;
	for (const auto& invalid : {"01", "017R018", "R", "256", "01x"}) {
		EXPECT_THROW(agent.setC2Animation(invalid), Exception);
		EXPECT_EQ(part->animation, animation);
		EXPECT_EQ(part->getFrameNo(), 1u);
	}
	agent.setC2Animation("");
	EXPECT_TRUE(part->animation.empty());
}

TEST_F(C2SkeletonState, ByteAnimationKeepsItsOriginalRepeatMarkerAndOffset) {
	auto agent = std::make_shared<BodyAgent>();
	auto s = saved();
	s.poses[18] = "233333333333333";
	agent->restoreC2Body(s, [&](const auto&) { return gallery(); });
	agent->setC2Animation("017R");
	caosScript script("c3", "animation-test");
	script.parse("anim [17 18 255 1]");
	caosVM vm(agent.get());
	vm.runEntirely(script.installer);
	auto* part = dynamic_cast<AnimatablePart*>(agent->part(0));
	part->tick();
	part->tick();
	part->tick();
	EXPECT_EQ(part->getFrameNo(), 2u);
	EXPECT_EQ(agent->getPose(1), 3u);
}

TEST_F(C2SkeletonState, RestoresAllFacingDirectionsAndRightFoot) {
	BodyAgent agent;
	auto s = saved();
	s.downfoot_right = 1;
	s.eyes_open = 0;
	s.limbs[0]->pose_plus_base += 10;
	const unsigned int expected[] = {3, 2, 0, 1};
	for (unsigned int direction = 0; direction < 4; ++direction) {
		s.direction = direction;
		agent.restoreC2Body(s, [&](const auto&) { return gallery(); });
		EXPECT_EQ(agent.facing(), expected[direction]);
		EXPECT_FALSE(agent.isLeftFootDown());
		EXPECT_TRUE(agent.getEyesClosed());
		EXPECT_EQ(agent.x + agent.attachmentX(12, 1), 487);
	}
}

TEST_F(C2SkeletonState, InvalidReplacementDoesNotDamageRestoredBody) {
	BodyAgent agent;
	auto s = saved();
	auto sprite = gallery();
	auto load = [&](const auto&) { return sprite; };
	agent.restoreC2Body(s, load);
	const auto old_pixels = agent.image(1).data(29);
	const auto old_x = agent.x;
	s.gait_animations[3] = "256R";
	EXPECT_THROW(agent.restoreC2Body(s, load), Exception);
	EXPECT_EQ(agent.gaitStep(3, 7), 24u);
	s = saved();
	s.limbs[1]->next.reset();
	EXPECT_THROW(agent.restoreC2Body(s, load), Exception);
	s = saved();
	sprite->images.resize(249);
	EXPECT_THROW(agent.restoreC2Body(s, load), Exception);
	EXPECT_EQ(agent.image(1).data(29), old_pixels);
	EXPECT_EQ(agent.x, old_x);
	EXPECT_EQ(agent.poseText(17), "202222222222222");
}

TEST_F(C2SkeletonState, AcceptsAbsentTailButRejectsTruncatedTail) {
	BodyAgent agent;
	auto s = saved();
	auto sprite = gallery();
	sprite->images.resize(230);
	s.limbs[5].reset();
	agent.restoreC2Body(s, [&](const auto&) { return sprite; });
	EXPECT_FALSE(agent.hasTail());
	s = saved();
	s.limbs[5]->next.reset();
	EXPECT_THROW(agent.restoreC2Body(s, [&](const auto&) { return sprite; }), Exception);
	EXPECT_FALSE(agent.hasTail());
}

TEST_F(C2SkeletonState, RejectsOverflowingCoordinatesBeforeReplacingTheBody) {
	BodyAgent agent;
	auto s = saved();
	auto sprite = gallery();
	auto load = [&](const auto&) { return sprite; };
	agent.restoreC2Body(s, load);
	const auto old_x = agent.x;
	s.body->x = std::numeric_limits<int32_t>::max();
	s.limbs[0]->x = std::numeric_limits<int32_t>::min();
	EXPECT_THROW(agent.restoreC2Body(s, load), std::exception);
	EXPECT_EQ(agent.x, old_x);
	EXPECT_EQ(agent.image(1).data(29), sprite->data(29));
}

TEST_F(C2SkeletonState, PreservesVisibleBlinkEvenWhenEyeFlagRequestsOpenEyes) {
	BodyAgent agent;
	auto s = saved();
	s.eyes_open = 1;
	s.limbs[0]->pose_plus_base += 10;
	agent.restoreC2Body(s, [&](const auto&) { return gallery(); });
	EXPECT_TRUE(agent.getEyesClosed());
	EXPECT_EQ(agent.getFacialExpression(), 1u);
}

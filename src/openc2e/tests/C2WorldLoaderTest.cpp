#include "openc2e/CompoundPart.h"
#include "openc2e/Map.h"
#include "openc2e/MetaRoom.h"
#include "openc2e/C2WorldLoader.h"
#include "openc2e/Agent.h"
#include "openc2e/Engine.h"
#include "openc2e/World.h"
#include "openc2e/historyManager.h"
#include "fileformats/sfc/C2SFCFile.h"
#include "fileformats/sfc/C2CreatureData.h"
#include "fileformats/genomeFile.h"
#include "common/Exception.h"
#include "common/io/SpanReader.h"
#include "common/creaturesImage.h"
#include <gtest/gtest.h>

namespace {
class C2WorldReferenceTest : public testing::Test {
  protected:
	int previous_version = engine.version;
	std::string previous_game = engine.gametype;
	sfc::C2SFCFile saved;
	std::shared_ptr<sfc::ObjectV2> first = std::make_shared<sfc::ObjectV2>();
	std::shared_ptr<sfc::ObjectV2> second = std::make_shared<sfc::ObjectV2>();
	sfc::ObjectV2 outside;
	void SetUp() override {
		engine.version = 2;
		engine.gametype = "c2";
		saved.map = std::make_shared<sfc::MapDataV2>();
		saved.map->background = std::make_shared<sfc::CGalleryV1>();
		first->unid = 1; second->unid = 2;
		saved.objects = {first, second};
	}
	void TearDown() override { engine.version = previous_version; engine.gametype = previous_game; }
	void rejects(const std::string& message, const C2WorldAssets& assets = {}) {
		const auto agents = world.agents.size();
		const auto rooms = world.map->getMetaRoomCount();
		const auto paused = world.paused;
		try {
			loadC2WorldSnapshot(saved, assets);
			FAIL() << "Accepted invalid world references";
		} catch (const Exception& error) {
			EXPECT_EQ(std::string(error.what()), message);
		}
		EXPECT_EQ(world.agents.size(), agents);
		EXPECT_EQ(world.map->getMetaRoomCount(), rooms);
		EXPECT_EQ(world.paused, paused);
	}
};
}

TEST_F(C2WorldReferenceTest, InvalidCalendarLeavesTheWorldUntouched) {
	const auto tick = world.worldtickcount;
	const auto time = world.timeofday, day = world.dayofseason, season = world.season, year = world.year;
	saved.map->reserved[0] = 5;
	rejects("Invalid saved C2 calendar");
	saved.map->reserved[0] = 0; saved.map->reserved[1] = 16;
	rejects("Invalid saved C2 calendar");
	EXPECT_EQ(world.worldtickcount, tick);
	EXPECT_EQ(world.timeofday, time); EXPECT_EQ(world.dayofseason, day);
	EXPECT_EQ(world.season, season); EXPECT_EQ(world.year, year);
}

TEST_F(C2WorldReferenceTest, MissingObjectLinksAreRejectedBeforeConstruction) {
	for (auto** link : {&first->object_pointer, &first->carrier, &saved.script_state.selected_creature}) {
		*link = &outside;
		rejects("C2 snapshot refers to an object outside its world");
		*link = nullptr;
	}
}

TEST_F(C2WorldReferenceTest, MissingBubbleCreatorIsRejectedBeforeConstruction) {
	auto bubble = std::make_shared<sfc::BubbleV2>();
	bubble->unid = 3;
	bubble->creator_id = 99;
	saved.objects.push_back(bubble);
	rejects("Saved C2 bubble has no creator");
}

TEST_F(C2WorldReferenceTest, DuplicateIdentitiesAndInvalidSelectionAreRejectedBeforeConstruction) {
	second->unid = first->unid;
	rejects("Duplicate object identity in C2 snapshot");
	second->unid = 2;
	saved.script_state.selected_creature = first.get();
	rejects("Selected C2 creature is not a creature");
	saved.script_state.selected_creature = nullptr;
	auto creature = std::make_shared<sfc::CreatureV2>();
	saved.objects.push_back(creature);
	rejects("Missing or duplicate creature identity in C2 snapshot");
	creature->moniker = "1ABC";
	auto duplicate = std::make_shared<sfc::CreatureV2>();
	duplicate->moniker = "1ABC";
	saved.objects.push_back(duplicate);
	rejects("Missing or duplicate creature identity in C2 snapshot");
}

TEST_F(C2WorldReferenceTest, CarrierCyclesAndOverlappingHeldObjectsAreRejectedBeforeConstruction) {
	first->carrier = second.get(); second->carrier = first.get();
	rejects("Cyclic carrier links in C2 save");
	first->carrier = first.get(); second->carrier = nullptr;
	rejects("Cyclic carrier links in C2 save");
	first->carrier = second.get();
	auto third = std::make_shared<sfc::ObjectV2>();
	third->carrier = second.get(); saved.objects.push_back(third);
	rejects("Multiple held objects share one carrier in C2 snapshot");
}

TEST_F(C2WorldReferenceTest, MissingMacroLinksAndDuplicateOwnersAreRejectedBeforeConstruction) {
	auto macro = std::make_shared<sfc::MacroV2>();
	macro->owner = first.get(); macro->self_destruct = 1;
	saved.script_state.macros = {nullptr, macro};
	for (auto** link : {&macro->owner, &macro->from, &macro->target, &macro->it}) {
		auto* original = *link;
		*link = &outside;
		rejects("C2 snapshot refers to an object outside its world");
		*link = original;
	}
	macro->exec = first.get();
	rejects("Unsupported C2 macro ownership or execution state");
	macro->exec = nullptr;
	saved.script_state.macros.push_back(std::make_shared<sfc::MacroV2>(*macro));
	rejects("Multiple active C2 macros for one owner are not supported");
}

TEST_F(C2WorldReferenceTest, MissingCreatureDependenciesLeaveTheWorldUntouched) {
	auto creature = std::make_shared<sfc::CreatureV2>();
	creature->moniker = "1ABC";
	saved.objects = {creature};
	rejects("Saved C2 creature is missing its brain, biochemistry or body");
	creature->brain = std::make_shared<sfc::CBrainV2>();
	creature->biochemistry = std::make_shared<sfc::CBiochemistryV2>();
	creature->body = std::make_shared<sfc::BodyV2>();
	creature->body->gallery = std::make_shared<sfc::CGalleryV1>();
	creature->body->gallery->filename = "1ABC";
	rejects("Missing C2 genome resolver");
	C2WorldAssets assets;
	assets.genome = [](const std::string&) { return std::shared_ptr<genomeFile>(); };
	rejects("Missing C2 creature genome", assets);
	const std::array<uint8_t, 8> dna = {'d', 'n', 'a', '2', 'g', 'e', 'n', 'd'};
	auto genome = std::make_shared<genomeFile>();
	SpanReader input(dna); input >> *genome;
	unsigned genome_loads = 0, image_loads = 0;
	assets.genome = [&](const std::string& moniker) {
		EXPECT_EQ(moniker, "1ABC"); ++genome_loads; return genome;
	};
	rejects("Missing C2 composite sprite resolver", assets);
	assets.composite = [&](const std::string& name) {
		EXPECT_EQ(name, "1ABC"); ++image_loads;
		return std::shared_ptr<creaturesImage>();
	};
	rejects("Missing C2 composite sprite", assets);
	EXPECT_EQ(image_loads, 1u);
	EXPECT_EQ(genome_loads, 2u);
}

TEST_F(C2WorldReferenceTest, SharedLimbRecordsAreRejectedWithoutReloadingTheirCommonSprite) {
	auto creature = std::make_shared<sfc::CreatureV2>();
	creature->moniker = "1ABC";
	creature->brain = std::make_shared<sfc::CBrainV2>();
	creature->biochemistry = std::make_shared<sfc::CBiochemistryV2>();
	creature->body = std::make_shared<sfc::BodyV2>();
	creature->body->gallery = std::make_shared<sfc::CGalleryV1>();
	creature->body->gallery->filename = "1ABC";
	auto limb = std::make_shared<sfc::LimbV2>();
	limb->gallery = creature->body->gallery;
	creature->limbs[0] = creature->limbs[1] = limb;
	saved.objects = {creature};
	const std::array<uint8_t, 8> dna = {'d', 'n', 'a', '2', 'g', 'e', 'n', 'd'};
	auto genome = std::make_shared<genomeFile>();
	SpanReader input(dna); input >> *genome;
	unsigned image_loads = 0;
	C2WorldAssets assets;
	assets.genome = [&](const std::string&) { return genome; };
	assets.composite = [&](const std::string&) {
		++image_loads; return std::make_shared<creaturesImage>("test");
	};
	rejects("Cyclic or shared limbs in C2 snapshot", assets);
	EXPECT_EQ(image_loads, 1u);
}

TEST(C2WorldLoader, RegistersExistingMonikerWithoutGeneratingIdentityOrLifeEvents) {
	historyManager history;
	auto genome = std::make_shared<genomeFile>();
	auto genus = std::make_unique<creatureGenusGene>(2);
	genus->genus = 0;
	genome->genes.push_back(std::move(genus));
	auto& record = history.restoreMoniker("1ABC", genome);
	EXPECT_EQ(history.findMoniker(genome), "1ABC");
	EXPECT_EQ(record.genome.lock(), genome);
	EXPECT_EQ(record.genus, 1);
	EXPECT_TRUE(record.events.empty());
	record.name = "Existing name";
	EXPECT_THROW(history.restoreMoniker("1ABC", genome), Exception);
	EXPECT_EQ(history.getMoniker("1ABC").name, "Existing name");
	EXPECT_THROW(history.restoreMoniker("", genome), Exception);
	EXPECT_THROW(history.restoreMoniker("2ABC", nullptr), Exception);
	EXPECT_FALSE(history.hasMoniker("2ABC"));
}

TEST(C2WorldLoader, RejectsWrongEngineAndMissingMapWithoutCallingAssetResolvers) {
	const auto old_game = engine.gametype;
	const auto old_version = engine.version;
	const auto before = world.agents.size();
	sfc::C2SFCFile saved;
	C2WorldAssets assets;
	engine.gametype = "c1"; engine.version = 1;
	EXPECT_THROW(loadC2WorldSnapshot(saved, assets), Exception);
	engine.gametype = "c2"; engine.version = 2;
	EXPECT_THROW(loadC2WorldSnapshot(saved, assets), Exception);
	EXPECT_EQ(world.agents.size(), before);
	engine.gametype = old_game; engine.version = old_version;
}

TEST(C2WorldLoader, HeldObjectUsesC2CarrierPlaneWithoutChangingOtherEngines) {
	class PlainAgent : public Agent {
	  public:
		PlainAgent() : Agent(2, 1, 1, 1000) {}
		CompoundPart* part(unsigned int) override { return nullptr; }
	};
	auto carrier = std::make_shared<PlainAgent>();
	auto held = std::make_shared<PlainAgent>();
	held->carriedby = carrier.get();
	const auto version = engine.version;
	engine.version = 1; EXPECT_EQ(held->getZOrder(), 1000u);
	engine.version = 2; EXPECT_EQ(held->getZOrder(), 1001u);
	engine.version = 3; EXPECT_EQ(held->getZOrder(), 900u);
	engine.version = version;
}


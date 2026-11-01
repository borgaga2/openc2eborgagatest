#include "openc2e/creatures/oldCreature.h"
#include "openc2e/creatures/oldBrain.h"
#include "openc2e/creatures/CreatureAgent.h"
#include "fileformats/sfc/C2CreatureData.h"
#include "fileformats/C2SpeechResources.h"
#include "openc2e/Engine.h"
#include "common/io/SpanReader.h"
#include "common/encoding.h"
#include <gtest/gtest.h>

namespace {
#include "C2ActionSpeechCases.h"
class SpeechAgent : public Agent, public CreatureAgent {
  public:
	SpeechAgent() : Agent(4, 1, 1, 0) {}
	CompoundPart* part(unsigned int) override { return nullptr; }
	Agent* getAgent() override { return this; }
};
class SpeechCreature : public c2Creature {
  public:
	using c2Creature::c2Creature;
	void focus(Agent* a) { attention = a; }
	void see(Agent* a) { chosenagents.at(a->category) = a; }
	void clear_inputs() { for (auto& lobe : getBrain()->lobes) lobe->wipe(); }
	unsigned input(unsigned tissue, unsigned cell) { return getBrain()->getLobeByTissue(tissue)->getNeuron(cell)->state; }
};
std::shared_ptr<genomeFile> genome() {
	const std::array<uint8_t, 8> bytes{'d','n','a','2','g','e','n','d'};
	SpanReader stream(bytes);
	auto g = std::make_shared<genomeFile>(); stream >> *g;
	for (unsigned i = 0; i < 8; ++i) {
		auto gene = std::make_unique<oldBrainLobeGene>(2);
		gene->x = gene->y = 0; gene->width = 40; gene->height = 1;
		gene->perceptflag = gene->nominalthreshold = gene->leakagerate = 0;
		gene->reststate = gene->inputgain = gene->flags = 0;
		std::fill_n(gene->staterule, 12, 0);
		const std::array<uint8_t, 256> zeros{}; SpanReader dendrites(zeros);
		dendrites >> gene->dendrite1 >> gene->dendrite2;
		g->genes.push_back(std::move(gene));
	}
	return g;
}
sfc::CreatureV2 words() {
	sfc::CreatureV2 s;
	s.vocabulary[0] = {"push","puh",100};
	s.vocabulary[1] = {"pull","pul",255};
	s.vocabulary[5] = {"eat","eet",60};
	s.vocabulary[16] = {"nori","no",255};
	s.vocabulary[17] = {"ball","baba",200};
	s.vocabulary[20] = {"food","foo",150};
	return s;
}
}

TEST(C2Language, RecognizesOwnInputAndOutputBeforeLaterEntriesWithoutAConfidenceGate) {
	SpeechAgent holder; SpeechCreature c(genome(),true,0,&holder,false);
	auto s = words(); s.vocabulary[0].strength = 0;
	s.vocabulary[18] = {"puh","push",255}; c.restoreC2Language(s);
	EXPECT_EQ(c.findC2Word("PUSH"),0);
	EXPECT_EQ(c.findC2Word("puh"),0);
	EXPECT_EQ(c.findC2Word("baba"),17);
	EXPECT_EQ(c.findC2Word(""),2);
	EXPECT_EQ(c.findC2Word("push!"),-1);
	EXPECT_EQ(c.findC2Word("push\tfood"),-1);
	EXPECT_EQ(c.getC2WordInput(0),"push");
	EXPECT_EQ(c.getC2SpokenWord(0),"puh");
	EXPECT_TRUE(c.getC2WordInput(-1).empty());
	EXPECT_TRUE(c.getC2WordInput(86).empty());
}

TEST(C2Language, CommandInputsMatchOriginalParserObservations) {
	SpeechAgent holder, source;
	SpeechCreature c(genome(),true,0,&holder,false); c.restoreC2Language(words());
	struct Case { const char* text; int noun, verb; unsigned noun_signal, verb_signal; };
	// Captured at native 420f00, including space-only tokenization, output
	// aliases, last noun/verb wins, and confidence divided by 256.
	for (const auto& row : std::vector<Case>{
		{"push",-1,0,180,70}, {"PUSH",-1,0,180,70}, {"puh",-1,0,180,70},
		{"eat food",4,5,105,42}, {"food eat",4,5,105,42},
		{"ball food push pull",4,1,105,179}, {"nori push",-1,0,255,99},
		{" push",-1,0,180,70}, {"push  food",4,2,105,0}, {"baba eet",1,5,140,42}}) {
		SCOPED_TRACE(row.text); c.clear_inputs();
		auto focus = std::make_shared<SpeechAgent>(); focus->category = row.noun;
		c.focus(focus.get()); if (row.noun >= 0) c.see(focus.get());
		ASSERT_TRUE(c.hearC2Speech(&source,row.text));
		if (row.noun >= 0) {
			EXPECT_EQ(c.input(4,row.noun),row.noun_signal);
		}
		EXPECT_EQ(c.input(3,row.verb),row.verb_signal);
	}
	c.clear_inputs(); c.focus(nullptr);
	EXPECT_FALSE(c.hearC2Speech(&source,"push!"));
	EXPECT_FALSE(c.hearC2Speech(&source,"push\tfood"));
	EXPECT_EQ(c.input(3,0),0u);
}

TEST(C2Language, DefersVerbUntilAttentionReachesTheNounAndExpiresAfterFourChances) {
	SpeechAgent holder, source;
	SpeechCreature c(genome(),true,0,&holder,false); c.restoreC2Language(words());
	auto food = std::make_shared<SpeechAgent>(); food->category = 4; c.see(food.get());
	ASSERT_TRUE(c.hearC2Speech(&source,"eat food"));
	EXPECT_EQ(c.input(4,4),105u); EXPECT_EQ(c.input(3,5),0u);
	c.focus(food.get()); c.advanceC2SpeechCommand();
	EXPECT_EQ(c.input(3,5),42u);
	c.advanceC2SpeechCommand(); EXPECT_EQ(c.input(3,5),42u); // deliver once
	c.clear_inputs(); c.focus(nullptr);
	ASSERT_TRUE(c.hearC2Speech(&source,"eat food"));
	for (unsigned i=0;i<4;++i) c.advanceC2SpeechCommand();
	c.focus(food.get()); c.advanceC2SpeechCommand();
	EXPECT_EQ(c.input(3,5),0u);
}

TEST(C2Language, AttendingToTheSpeakerUsesTheSameStrengthAsBeingAddressedByName) {
	SpeechAgent holder;
	auto source = std::make_shared<SpeechAgent>();
	SpeechCreature c(genome(),true,0,&holder,false); c.restoreC2Language(words());
	c.focus(source.get());
	ASSERT_TRUE(c.hearC2Speech(source.get(),"push"));
	EXPECT_EQ(c.input(3,0),99u); // native (255 * 100) >> 8, rather than 70
}

TEST(C2Language, UnknownSpeechTeachesTheCurrentAttentionCategoryButNotWhileAsleep) {
	SpeechAgent holder, source;
	SpeechCreature c(genome(),true,0,&holder,false);
	auto original = words(); original.vocabulary[20] = {"old","goo",0};
	c.restoreC2Language(original);
	auto food = std::make_shared<SpeechAgent>(); food->category = 4; c.focus(food.get());
	auto previous_resources = engine.c2_speech_resources;
	engine.c2_speech_resources = std::make_shared<C2SpeechResources>();
	EXPECT_FALSE(c.hearC2Speech(&source,"apple")); // unknown on arrival
	sfc::CreatureV2 learned; c.updateC2Language(learned);
	EXPECT_EQ(learned.vocabulary[20].in,"apple");
	EXPECT_EQ(learned.vocabulary[20].out,"gpgp");
	EXPECT_EQ(learned.vocabulary[16].in,"nori");
	c.restoreC2Language(original); c.setAsleep(true);
	EXPECT_FALSE(c.hearC2Speech(&source,"apple"));
	c.updateC2Language(learned);
	EXPECT_EQ(learned.vocabulary[20].in,"old");
	engine.c2_speech_resources = std::move(previous_resources);
}

TEST(C2Language, ActionResponseMatches128OriginalCodeObservations) {
	SpeechAgent holder; SpeechCreature c(genome(),true,0,&holder,false);
	sfc::CreatureV2 saved;
	for (unsigned i=0;i<86;++i) saved.vocabulary[i] = {"i"+std::to_string(i),"w"+std::to_string(i),255};
	c.restoreC2Language(saved);
	C2SpeechResources resources;
	// These numeric maps accompany the independent synthetic-word oracle;
	// production reads them from the verified installed executable.
	resources.action_words = {75,1,2,3,4,5,6,7,8,9,10,11,12,13,255,255};
	resources.action_uses_noun = {1,1,1,1,1,1,1,1,0,1,0,0,1,1,0,0};
	for (const auto& row : native_action_speech_cases) {
		SCOPED_TRACE(std::to_string(row.decision)+":"+std::to_string(row.focus)+":"+std::to_string(row.seed));
		auto seed=row.seed;
		const auto actual=c.planC2ActionSpeech(row.decision,row.focus?5:-1,36,seed,resources);
		EXPECT_EQ(actual.text,row.text);
		EXPECT_EQ(actual.speak,row.speak);
		EXPECT_EQ(actual.teaching_words,row.teaching_words);
		EXPECT_EQ(seed,row.final_seed);
	}
}

TEST(C2Language, ActionPhraseLimitCountsOriginalEncodingBytesWithoutBreakingUtf8) {
	SpeechAgent holder; SpeechCreature c(genome(),true,0,&holder,false);
	auto saved=words(); const auto name=cp1252_to_utf8(std::string(24,'\xe9'));
	saved.vocabulary[16] = {name,name,255}; c.restoreC2Language(saved);
	C2SpeechResources resources; resources.action_words.fill(255); resources.action_words[1]=1;
	uint32_t seed=0;
	const auto actual=c.planC2ActionSpeech(1,-1,36,seed,resources);
	EXPECT_EQ(actual.text,name); EXPECT_TRUE(actual.speak);
	EXPECT_EQ(actual.teaching_words,(std::vector<int>{52,1}));
}

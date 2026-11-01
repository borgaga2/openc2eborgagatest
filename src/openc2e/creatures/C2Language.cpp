#include "oldCreature.h"
#include "oldBrain.h"
#include "Engine.h"
#include "Bubble.h"
#include "PointerAgent.h"
#include "World.h"
#include "common/Ascii.h"
#include "fileformats/C2SpeechResources.h"
#include "C2HearingRange.h"
#include "creatures/CreatureAgent.h"
#include "common/Random.h"
#include "common/encoding.h"
#include "caosValue.h"

#include <algorithm>
#include <stdexcept>

namespace {
void add_input(oldBrain& brain, unsigned tissue, int cell, unsigned amount) {
	auto* lobe = brain.getLobeByTissue(tissue);
	if (!lobe || cell < 0 || static_cast<unsigned>(cell) >= lobe->getNoNeurons()) return;
	auto* neuron = lobe->getNeuron(cell);
	neuron->state = static_cast<uint8_t>(std::min(255u, unsigned(neuron->state) + (amount & 255u)));
}
}

std::string c2Creature::getC2WordInput(int word) const {
	if (word < 0 || static_cast<size_t>(word) >= vocabulary.size()) return {};
	return vocabulary[word].input;
}

void c2Creature::showC2Speech(const std::string& text) {
	if (text.empty() || !parentagent || !parentagent->part(0)) return;
	Bubble::newBubble(parentagent, true, text);
	parentagent->speak(text);
}

void c2Creature::broadcastC2Word(int word, bool teaching) {
	if (engine.version != 2 || word < 0 || word >= 86 || !isAlive() || isAsleep()) return;
	for (const auto& agent : world.agents) {
		if (!agent || agent.get() == parentagent) continue;
		auto* holder = dynamic_cast<CreatureAgent*>(agent.get());
		if (holder && dynamic_cast<c2Creature*>(holder->getCreature()) && c2CanHear(agent.get(), parentagent))
			agent->queueScript(teaching ? 7 : 6, parentagent, caosValue(word));
	}
}

bool c2Creature::receiveC2Word(Agent* source, int word, const std::string& text) {
	if (!source || !isAlive() || isAsleep() || word < 0 || word >= 86 || !engine.c2_speech_resources) return false;
	const auto before = vocabulary[word].strength;
	if (!learnC2Word(word, text, engine.c2_speech_resources->pronunciation_rules)) return false;
	const bool from_hand = source == world.hand();
	// Native teaching echoes every changed association; unchanged associations
	// from other agents echo on one of six random draws. Echoes do not rebroadcast.
	if (from_hand || before != vocabulary[word].strength || ((rand_uint32() >> 17) * 6u) < 32768)
		showC2Speech(getC2SpokenWord(word));
	if (from_hand && word >= 16 && word < 56) add_input(*brain, 4, word - 16, 30);
	return true;
}

c2Creature::C2ActionSpeech c2Creature::planC2ActionSpeech(int decision, int focus_category, int speaker_category,
	uint32_t& state, const C2SpeechResources& resources) const {
	C2ActionSpeech result;
	auto draw = [&] { state = state * 1664525u + 1013904223u; return state; };
	if (((draw() >> 17) * 3u) < 32768) {
		result.text = getC2SpokenWord(16) + " ";
		result.teaching_words.push_back(16 + (speaker_category >= 0 && speaker_category < 40 ? speaker_category : 0));
	}
	if (decision < 0 || decision >= 16 || resources.action_words[decision] == 255) {
		result.text.clear();
		return result;
	}
	const int verb = resources.action_words[decision];
	result.text += getC2SpokenWord(verb);
	if (focus_category >= 0 && focus_category < 40 && resources.action_uses_noun[decision] && draw() < 0x80000000u) {
		result.text += " " + getC2SpokenWord(16 + focus_category);
		result.teaching_words.push_back(16 + focus_category);
	}
	// The original phrase buffer terminates at byte 24, in its CP-1252 encoding.
	try { result.text = cp1252_to_utf8(utf8_to_cp1252(result.text).substr(0, 24)); }
	catch (const std::domain_error&) { result.text.clear(); return result; }
	result.teaching_words.push_back(verb);
	result.speak = true;
	return result;
}

void c2Creature::sayC2Action() {
	if (!isAlive() || isAsleep() || !engine.c2_speech_resources) return;
	auto state = rand_uint32();
	const auto utterance = planC2ActionSpeech(decn, attention ? attention->category : -1,
		parentagent->category, state, *engine.c2_speech_resources);
	for (int word : utterance.teaching_words) broadcastC2Word(word, true);
	if (utterance.speak) showC2Speech(utterance.text);
}

int c2Creature::findC2Word(const std::string& text) const {
	const auto folded = to_ascii_lowercase(text);
	for (size_t i = 0; i < vocabulary.size(); ++i) {
		const auto& word = vocabulary[i];
		if (folded == to_ascii_lowercase(word.input) || folded == to_ascii_lowercase(word.output))
			return static_cast<int>(i);
	}
	return -1;
}

void c2Creature::applyC2SpeechCommand(int noun, int verb, unsigned noun_strength, unsigned verb_strength) {
	if (noun >= 0 && static_cast<size_t>(noun) < chosenagents.size() && chosenagents[noun])
		add_input(*brain, 4, noun, noun_strength);
	if (verb < 0) return;
	const int focus_category = attention ? attention->category : 0;
	if (noun >= 0 && noun != focus_category) {
		// Native C2 gives attention four subsequent brain frames to reach the
		// requested noun before delivering the verb (countdown starts at five).
		speech_noun = noun;
		speech_verb = verb;
		speech_strength = static_cast<uint8_t>(verb_strength);
		speech_ticks = 5;
	} else {
		add_input(*brain, 3, verb, verb_strength);
		speech_noun = -1;
	}
}

void c2Creature::advanceC2SpeechCommand() {
	if (speech_ticks) --speech_ticks;
	if (speech_noun >= 0 && speech_ticks && attention && attention->category == speech_noun) {
		add_input(*brain, 3, speech_verb, speech_strength);
		speech_noun = -1;
	}
}

bool c2Creature::hearC2Speech(Agent* source, const std::string& text) {
	if (!source || !isAlive() || text.empty()) return false;
	const bool from_hand = source == world.hand();
	int own_name = -1, noun_word = -1, other_word = -1;
	auto teach = [&](int id, const std::string& word) {
		receiveC2Word(source, id, word);
	};
	// C2 separates only on literal spaces. Repeated spaces produce empty
	// tokens, and the first matching input OR output entry wins, even at zero
	// confidence. Do not add punctuation removal or dictionary fallbacks.
	for (size_t begin = 0;;) {
		const auto end = text.find(' ', begin);
		const auto token = text.substr(begin, end == std::string::npos ? end : end - begin);
		const int word = findC2Word(token);
		if (word == 16) own_name = word;
		else if (word >= 17 && word <= 55) noun_word = word;
		else if (word >= 0) other_word = word;
		else if (attention && attention->category >= 0 && attention->category < 40)
			teach(16 + attention->category, token);
		else if (from_hand) teach(16, token);
		if (end == std::string::npos) break;
		begin = end + 1;
	}
	const bool recognized = own_name >= 0 || noun_word >= 0 || other_word >= 0;
	if (!recognized) handleStimulus(9);
	else if (from_hand) handleStimulus(10);
	else if (source->family == 4) handleStimulus(11);

	if (attention && noun_word == 16 + attention->category)
		teach(noun_word, vocabulary[noun_word].input);
	if (!attention && own_name == 16 && from_hand)
		teach(16, vocabulary[16].input);

	int noun = noun_word < 0 ? -1 : noun_word - 16;
	int verb = other_word >= 0 && other_word <= 15 ? other_word : -1;
	if (own_name == 16 && noun_word < 0 && other_word < 0 && !source->invisible())
		noun = source->category;
	if (other_word == 9) noun = 0;
	if (other_word == 73 || other_word == 74)
		handleStimulus((other_word == 73 ? 40 : 42) + (from_hand ? 0 : 1));
	if (other_word == 76) sayC2Action();
	// Entries 8 and 75 still need their native need-expression and
	// hand-directed object-selection routines.
	if (other_word == 8) verb = -1;
	const unsigned base = own_name >= 0 || attention.get() == source ? 255 : 180;
	const unsigned noun_strength = noun_word < 0 ? base : (base * (vocabulary[noun_word].strength & 255u)) >> 8;
	const unsigned verb_strength = other_word < 0 ? base : (base * (vocabulary[other_word].strength & 255u)) >> 8;
	applyC2SpeechCommand(noun, verb, noun_strength, verb_strength);
	return recognized;
}

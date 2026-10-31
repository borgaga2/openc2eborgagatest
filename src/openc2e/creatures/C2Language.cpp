#include "oldCreature.h"
#include "oldBrain.h"
#include "Engine.h"
#include "Bubble.h"
#include "PointerAgent.h"
#include "World.h"
#include "common/Ascii.h"
#include "fileformats/C2SpeechResources.h"

#include <algorithm>

namespace {
void add_input(oldBrain& brain, unsigned tissue, int cell, unsigned amount) {
	auto* lobe = brain.getLobeByTissue(tissue);
	if (!lobe || cell < 0 || static_cast<unsigned>(cell) >= lobe->getNoNeurons()) return;
	auto* neuron = lobe->getNeuron(cell);
	neuron->state = static_cast<uint8_t>(std::min(255u, unsigned(neuron->state) + (amount & 255u)));
}
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
		if (!isAsleep() && engine.c2_speech_resources &&
			learnC2Word(id, word, engine.c2_speech_resources->pronunciation_rules) && from_hand) {
			const auto output = getC2SpokenWord(id);
			if (!output.empty()) {
				Bubble::newBubble(parentagent, true, output);
				parentagent->speak(output);
			}
		}
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
	// Vocabulary entries 8, 75 and 76 invoke native question/response
	// routines. Their phrase generation is still incomplete; do not turn
	// them into an ordinary action simply because entry 8 is in verb range.
	if (other_word == 8) verb = -1;
	const unsigned base = own_name >= 0 || attention.get() == source ? 255 : 180;
	const unsigned noun_strength = noun_word < 0 ? base : (base * (vocabulary[noun_word].strength & 255u)) >> 8;
	const unsigned verb_strength = other_word < 0 ? base : (base * (vocabulary[other_word].strength & 255u)) >> 8;
	applyC2SpeechCommand(noun, verb, noun_strength, verb_strength);
	return recognized;
}

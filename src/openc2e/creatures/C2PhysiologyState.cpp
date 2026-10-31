#include "oldCreature.h"
#include "fileformats/C2SpeechResources.h"
#include "common/Exception.h"
#include "common/Ascii.h"
#include "fileformats/sfc/C2CreatureData.h"

#include <algorithm>

void c2Creature::restoreC2Physiology(const sfc::CreatureV2& saved) {
	if ((saved.sex != 1 && saved.sex != 2) || (saved.sex == 2) != female)
		throw Exception("Saved C2 physiology does not match the creature's sex");
	if (saved.age > senile)
		throw Exception("Saved C2 physiology has an invalid life stage");
	const auto& state = saved.physiological_state;
	fertility_output = state[0];
	pregnant = state[1];
	std::copy_n(state.begin() + 2, 10, senses);
	senses[12] = state[12];
	senses[13] = state[13];
	std::copy_n(state.begin() + 14, 7, lifestageloci);
	std::copy_n(state.begin() + 21, 8, floatingloci);
	fertile = state[29];
	receptive = state[30];
	mutationchance = state[31];
	mutationdegree = state[32];
	dead = state[33];
	for (size_t i = 0; i < 8; ++i) {
		involaction[i] = state[34 + i * 2];
		involactionlatency[i] = state[35 + i * 2];
	}
	std::copy_n(state.begin() + 50, 16, gaitloci);
	std::copy_n(state.begin() + 66, 17, drives);
	stage = static_cast<lifestage>(saved.age);
	age = saved.age_ticks;
	biochemticks = saved.biotick;
	// C2 processes the brain on odd biochemistry samples. Our scheduler uses
	// world frames, so preserve that relative phase rather than restarting it.
	ticks = (saved.biotick + 1u) * 2u;
	life_state_signal = saved.life_state;
	alive = saved.life_state == 0;
	retained_asleep_state = saved.asleep;
	asleep = saved.asleep != 0;
	// The original deserializer enables an already-created creature without
	// replaying born/aged/died callbacks or expressing initialization genes.
	tickage = true;
}

void c2Creature::updateC2Physiology(sfc::CreatureV2& saved) const {
	auto& state = saved.physiological_state;
	state[0] = fertility_output;
	state[1] = pregnant;
	std::copy_n(senses, 10, state.begin() + 2);
	state[12] = senses[12];
	state[13] = senses[13];
	std::copy_n(lifestageloci, 7, state.begin() + 14);
	std::copy_n(floatingloci, 8, state.begin() + 21);
	state[29] = fertile;
	state[30] = receptive;
	state[31] = mutationchance;
	state[32] = mutationdegree;
	state[33] = dead;
	for (size_t i = 0; i < 8; ++i) {
		state[34 + i * 2] = involaction[i];
		state[35 + i * 2] = static_cast<uint8_t>(involactionlatency[i]);
	}
	std::copy_n(gaitloci, 16, state.begin() + 50);
	std::copy_n(drives, 17, state.begin() + 66);
	saved.sex = female ? 2 : 1;
	saved.age = static_cast<uint8_t>(stage);
	saved.age_ticks = age;
	saved.biotick = biochemticks;
	saved.life_state = alive ? 0 : (life_state_signal ? life_state_signal : 255);
	saved.asleep = asleep ? (retained_asleep_state ? retained_asleep_state : 1) : 0;
}

void c2Creature::updateFertilitySignal() {
	// C2 latches a gamete above 160 and releases it below 80. The emitter is
	// a cached boolean signal, distinct from the fertility receptor's input.
	if (fertile > 160)
		fertility_output = 255;
	else if (fertile < 80)
		fertility_output = 0;
}

void c2Creature::restoreC2Language(const sfc::CreatureV2& saved) {
	for (size_t i = 0; i < vocabulary.size(); ++i) {
		const auto& word = saved.vocabulary[i];
		vocabulary[i] = {word.in, word.out, word.strength};
	}
}

void c2Creature::updateC2Language(sfc::CreatureV2& saved) const {
	for (size_t i = 0; i < vocabulary.size(); ++i) {
		const auto& word = vocabulary[i];
		saved.vocabulary[i] = {word.input, word.output, word.strength};
	}
}

std::string c2Creature::getC2SpokenWord(int word) const {
	// Native SAY# passes the creature's output string at +0x1e01 + 56*word
	// to speech. It does not substitute the global dictionary or gate on
	// the association strength. Unknown output remains empty.
	if (word < 0 || static_cast<size_t>(word) >= vocabulary.size()) return {};
	return vocabulary[word].output;
}

bool c2Creature::reinforceC2Word(int word, const std::string& input,
	const std::vector<std::pair<std::string, std::string>>& pronunciation_rules) {
	if (word < 0 || static_cast<size_t>(word) >= vocabulary.size()) return false;
	auto& entry = vocabulary[word];
	// This handles reinforcement of the existing association only. New or
	// competing associations need the separate native initial-pronunciation
	// path; callers must not silently treat this as complete word teaching.
	// Native association matching uses _mbsicmp; retain ASCII case folding
	// here. Non-ASCII locale-specific folding still needs auditing.
	if (to_ascii_lowercase(entry.input) != to_ascii_lowercase(input)) return false;
	const unsigned strength = std::min(255u, (entry.strength & 255u) + 60u);
	entry.strength = strength;
	if (strength >= 80) {
		entry.output = entry.input;
		// Native 0x420560 tries rules in table order, replacing only the
		// first occurrence of the first matching rule, then stops.
		for (const auto& [from, to] : pronunciation_rules) {
			if (from.empty()) continue;
			const auto at = entry.output.find(from);
			if (at == std::string::npos) continue;
			entry.output.replace(at, from.size(), to);
			break;
		}
	}
	if (strength >= 160) entry.output = entry.input;
	return true;
}

bool c2Creature::learnC2Word(int word, const std::string& input,
	const std::vector<std::pair<std::string, std::string>>& pronunciation_rules) {
	if (word < 0 || static_cast<size_t>(word) >= vocabulary.size() || input.empty() || input.size() > 24)
		return false;
	// The native path works on its 25-byte strings. Non-ASCII input and
	// malformed syllables remain unsupported until locale behavior is proved.
	for (unsigned char c : input) if (c >= 128) return false;
	auto& entry = vocabulary[word];
	if (to_ascii_lowercase(entry.input) == to_ascii_lowercase(input))
		return reinforceC2Word(word, input, pronunciation_rules);
	const unsigned previous = entry.strength & 255u;
	if (previous > 45) { entry.strength = previous - 45; return true; }
	const std::string vowels = "aeiouAEIOU";
	const std::string consonants = "bcdfghjklmnpqrstvwxyzBCDFGHJKLMNPQRSTVWXYZ";
	const bool starts_vowel = vowels.find(input[0]) != std::string::npos;
	size_t end = 0;
	while (end < input.size() && (vowels.find(input[end]) != std::string::npos) == starts_vowel) ++end;
	if (end == input.size()) return false;
	if ((starts_vowel ? consonants : vowels).find(input[end]) != std::string::npos) ++end;
	// Native 0x4203b0 advances both pointers before copying: the existing
	// first output byte survives. Preserve this rather than inventing a typo.
	std::string output = entry.output.empty() ? std::string() : entry.output.substr(0, 1) + input.substr(1, end - 1);
	if ((input.size() > 5 && output.size() < 5) || (input.size() <= 5 && output.size() < 3)) output += output;
	if (output.empty()) {
		output = input;
		for (const auto& [from, to] : pronunciation_rules) {
			if (from.empty()) continue;
			const auto at = output.find(from);
			if (at != std::string::npos) { output.replace(at, from.size(), to); break; }
		}
	}
	entry = {input, output, 60};
	return true;
}

void c2Creature::initializeC2Language(const C2SpeechResources& resources, uint32_t& state) {
	if (genus > 2) throw Exception("Unsupported C2 speech genus");
	std::array<C2Word, 86> words;
	auto draw = [&] { state = state * 1664525u + 1013904223u; return state; };
	auto fill = [&](unsigned begin, unsigned count, unsigned field) {
		for (unsigned i = begin; i < begin + count; ++i) {
			const unsigned variant = draw() >> 30;
			const auto& text = resources.babble[genus * 4 + variant][field];
			const unsigned strength = 30 + (((draw() >> 17) * 21u) >> 15);
			words[i] = {text, text, strength};
		}
	};
	fill(0, 16, 0); fill(16, 40, 1); fill(56, 17, 2);
	fill(73, 4, 3); fill(78, 4, 4); fill(16, 1, 5);
	vocabulary = std::move(words);
}

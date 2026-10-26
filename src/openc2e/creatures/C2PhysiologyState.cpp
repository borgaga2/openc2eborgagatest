#include "oldCreature.h"
#include "common/Exception.h"
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

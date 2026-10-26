#pragma once

#include "C2BiochemistryData.h"
#include "C2BodyData.h"
#include "C2BrainData.h"
#include "CInstinct.h"

namespace sfc {

// C2's inline history uses raw four-byte monikers, unlike C1's hex strings.
struct CreatureHistoryV2 {
	std::array<std::string, 3> monikers, names;
	std::array<std::string, 6> text;
	std::array<uint32_t, 3> state_before_description{};
	std::string description;
	std::array<uint32_t, 7> state_after_description{};
	std::array<uint8_t, 256> state{};
	std::array<uint32_t, 9> reserved{};

	template <typename Archive>
	void serialize(Archive& ar) {
		for (size_t i = 0; i < monikers.size(); ++i) {
			ar.ascii_dword(monikers[i]);
			ar.win1252_mfcstring(names[i]);
		}
		for (auto& value : text)
			ar.win1252_mfcstring(value);
		for (auto& value : state_before_description)
			ar(value);
		ar.win1252_mfcstring(description);
		for (auto& value : state_after_description)
			ar(value);
		ar(std::span<uint8_t>(state));
		for (auto& value : reserved)
			ar(value);
	}
};

struct CreatureV2 : ObjectV2 {
	std::string moniker, mother, father;
	std::array<std::string, 2> parent_names;
	std::shared_ptr<BodyV2> body;
	std::array<std::shared_ptr<LimbV2>, 6> limbs;
	uint8_t direction = 0, downfoot_left = 0;
	int32_t foot_x = 0, foot_y = 0;
	uint32_t z_order = 0;
	std::string current_pose;
	uint8_t expression = 0, eyes_open = 0, asleep = 0;
	uint32_t reserved_after_asleep = 0;
	std::array<std::string, 256> poses;
	std::array<std::string, 16> gait_animations;
	std::array<uint8_t, 31> speech_state{};
	struct Word {
		std::string in, out;
		uint32_t strength = 0;
	};
	std::array<Word, 86> vocabulary;
	std::array<std::array<int32_t, 2>, 40> object_positions{};
	std::array<std::array<uint8_t, 12>, 46> stimuli{};
	std::shared_ptr<CBrainV2> brain;
	std::shared_ptr<CBiochemistryV2> biochemistry;
	uint8_t sex = 0, age = 0;
	uint32_t biotick = 0;
	std::string gamete, zygote;
	uint8_t life_state = 0;
	uint32_t age_ticks = 0;
	// Preserve the C2-specific drive/locus state before the instinct table.
	std::array<uint8_t, 83> physiological_state{};
	std::vector<std::shared_ptr<CInstinctV1>> instincts;
	uint32_t dreaming = 0;
	std::array<std::array<uint32_t, 17>, 40> goals{};
	std::array<ObjectV2*, 3> object_references{};
	std::array<uint32_t, 2> state_after_references{};
	uint32_t voice_state = 0;
	std::array<std::array<uint32_t, 27>, 3> voices_lookup{};
	struct Voice {
		std::array<uint8_t, 4> name{};
		uint32_t delay_ticks = 0;
	};
	std::array<Voice, 32> voices{};
	CreatureHistoryV2 history;

	template <typename Archive>
	void serialize(Archive& ar) {
		ObjectV2::serialize(ar);
		ar.ascii_dword(moniker);
		ar.ascii_dword(mother);
		ar.ascii_dword(father);
		for (auto& name : parent_names)
			ar.win1252_mfcstring(name);
		ar(body);
		for (auto& limb : limbs)
			ar(limb);
		ar(direction); ar(downfoot_left); ar(foot_x); ar(foot_y); ar(z_order);
		ar.ascii_mfcstring(current_pose);
		ar(expression); ar(eyes_open); ar(asleep); ar(reserved_after_asleep);
		for (auto& pose : poses)
			ar.ascii_mfcstring(pose);
		for (auto& gait : gait_animations)
			ar.ascii_mfcstring(gait);
		ar(std::span<uint8_t>(speech_state));
		for (auto& word : vocabulary) {
			ar.win1252_mfcstring(word.in);
			ar.win1252_mfcstring(word.out);
			ar(word.strength);
		}
		for (auto& position : object_positions) {
			ar(position[0]); ar(position[1]);
		}
		for (auto& stimulus : stimuli)
			ar(std::span<uint8_t>(stimulus));
		ar(brain); ar(biochemistry);
		ar(sex); ar(age); ar(biotick);
		ar.ascii_dword(gamete); ar.ascii_dword(zygote);
		ar(life_state); ar(age_ticks);
		ar(std::span<uint8_t>(physiological_state));
		uint32_t count = numeric_cast<uint32_t>(instincts.size());
		ar(count);
		if (count > 20)
			throw Exception("C2 creature has too many instincts");
		if constexpr (std::is_same_v<Archive, MFCReader>)
			instincts.resize(count);
		ar(dreaming);
		for (auto& instinct : instincts)
			ar(instinct);
		for (auto& goal : goals)
			for (auto& drive : goal)
				ar(drive);
		for (auto& reference : object_references)
			ar(reference);
		for (auto& value : state_after_references)
			ar(value);
		ar(voice_state);
		for (auto& group : voices_lookup)
			for (auto& value : group)
				ar(value);
		for (auto& voice : voices) {
			ar(std::span<uint8_t>(voice.name));
			ar(voice.delay_ticks);
		}
		history.serialize(ar);
	}
};

template <typename Archive>
void register_c2_creature_classes(Archive& ar) {
	register_c2_body_classes(ar);
	register_c2_brain_class(ar);
	register_c2_biochemistry_classes(ar);
	ar.template register_class<CInstinctV1>("CInstinct", 1);
	ar.template register_class<CreatureV2>("Creature", 2);
}

} // namespace sfc

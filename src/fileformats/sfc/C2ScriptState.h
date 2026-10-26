#pragma once

#include "C2ObjectData.h"

namespace sfc {

struct MacroV2 : MFCObject {
	uint32_t self_destruct = 0, instant = 0;
	// Keep the additional length field verbatim; it can differ from the
	// encoded MFC string length in original C2 files.
	uint32_t stored_script_length = 0;
	std::string script;
	uint32_t instruction_pointer = 0;
	std::array<int32_t, 20> stack{};
	uint32_t stack_pointer = 0;
	std::array<int32_t, 100> variables{};
	ObjectV2* owner = nullptr;
	ObjectV2* from = nullptr;
	ObjectV2* exec = nullptr;
	ObjectV2* target = nullptr;
	ObjectV2* it = nullptr;
	int32_t part = 0;
	std::array<uint8_t, 4> subroutine_label{};
	uint32_t subroutine_address = 0;
	int32_t wait = 0;
	std::array<uint8_t, 16> reserved{};

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(self_destruct);
		ar(instant);
		ar(stored_script_length);
		ar.win1252_mfcstring(script);
		ar(instruction_pointer);
		for (auto& value : stack)
			ar(value);
		ar(stack_pointer);
		for (auto& value : variables)
			ar(value);
		ar(owner);
		ar(from);
		ar(exec);
		ar(target);
		ar(it);
		ar(part);
		ar(std::span<uint8_t>(subroutine_label));
		ar(subroutine_address);
		ar(wait);
		ar(std::span<uint8_t>(reserved));
	}
};

struct FavoritePlaceV2 {
	std::string name;
	int16_t x = 0, y = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar.win1252_mfcstring(name);
		ar(x);
		ar(y);
	}
};

// The section following the global script definitions in a C2 world archive.
// Object references share the map/object section's MFC archive context.
struct ScriptStateV2 {
	int32_t scroll_x = 0, scroll_y = 0;
	ObjectV2* selected_creature = nullptr;
	std::array<FavoritePlaceV2, 6> favorite_places;
	uint16_t reserved_after_places = 0;
	std::vector<std::string> speech_history;
	std::vector<std::string> place_history;
	std::vector<std::shared_ptr<MacroV2>> macros;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(scroll_x);
		ar(scroll_y);
		ar(selected_creature);
		for (auto& place : favorite_places)
			place.serialize(ar);
		ar(reserved_after_places);
		ar.size_u16(speech_history);
		for (auto& speech : speech_history)
			ar.win1252_mfcstring(speech);
		ar.size_u16(place_history);
		for (auto& place : place_history)
			ar.win1252_mfcstring(place);
		ar.size_u32(macros);
		for (auto& macro : macros)
			ar(macro);
	}
};

template <typename Archive>
void register_c2_macro_class(Archive& ar) {
	ar.template register_class<MacroV2>("Macro", 2);
}

} // namespace sfc

#pragma once

#include "CGallery.h"
#include "WinRect.h"
#include "common/Exception.h"

#include <array>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace sfc {

struct ScriptV2 {
	uint8_t genus = 0, family = 0;
	uint16_t event = 0, species = 0;
	std::string text;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(genus);
		ar(family);
		ar(event);
		ar(species);
		ar.win1252_mfcstring(text);
	}
};

struct EntityV2 : MFCObject {
	std::shared_ptr<CGalleryV1> gallery;
	uint8_t pose_plus_base = 0, base = 0;
	int32_t plane = 0, x = 0, y = 0;
	uint8_t has_animation = 0, animation_frame = 0;
	// Retain padding after the terminating NUL: original archives can contain
	// nonzero bytes there, which must survive an unmodified read/write cycle.
	std::array<uint8_t, 99> animation{};

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(gallery);
		ar(pose_plus_base);
		ar(base);
		ar(plane);
		ar(x);
		ar(y);
		ar(has_animation);
		if (has_animation > 1)
			throw Exception("Invalid C2 entity animation flag");
		if (has_animation) {
			ar(animation_frame);
			ar(std::span<uint8_t>(animation));
		}
	}
};

struct ObjectV2 : MFCObject {
	uint8_t genus = 0, family = 0;
	uint16_t reserved_classifier = 0, species = 0;
	uint32_t unid = 0;
	uint8_t movement_status = 0;
	uint16_t attributes = 0, reserved_attributes = 0;
	WinRect limit{};
	ObjectV2* carrier = nullptr;
	uint8_t active = 0;
	std::shared_ptr<CGalleryV1> gallery;
	uint32_t timer_interval = 0, timer_state = 0;
	ObjectV2* object_pointer = nullptr;
	std::array<uint8_t, 4> current_sound{};
	std::array<uint32_t, 100> variables{};
	uint8_t size = 0;
	uint32_t range = 0, gravity_data = 0, acceleration = 0;
	int32_t velocity_x = 0, velocity_y = 0;
	uint32_t restitution = 0, aerodynamic_drag = 0;
	std::array<uint8_t, 6> reserved_physics{};
	uint8_t threat = 0, flags = 0;
	std::vector<ScriptV2> scripts;

	template <typename Archive>
	void serialize(Archive& ar) {
		ar(genus);
		ar(family);
		ar(reserved_classifier);
		ar(species);
		ar(unid);
		ar(movement_status);
		ar(attributes);
		ar(reserved_attributes);
		ar(limit.left);
		ar(limit.top);
		ar(limit.right);
		ar(limit.bottom);
		ar(carrier);
		ar(active);
		ar(gallery);
		ar(timer_interval);
		ar(timer_state);
		ar(object_pointer);
		ar(std::span<uint8_t>(current_sound));
		for (auto& value : variables)
			ar(value);
		ar(size);
		ar(range);
		ar(gravity_data);
		ar(acceleration);
		ar(velocity_x);
		ar(velocity_y);
		ar(restitution);
		ar(aerodynamic_drag);
		ar(std::span<uint8_t>(reserved_physics));
		ar(threat);
		ar(flags);
		ar.size_u32(scripts);
		for (auto& script : scripts)
			script.serialize(ar);
	}
};

struct SimpleObjectV2 : ObjectV2 {
	std::shared_ptr<EntityV2> entity;
	int32_t plane = 0;
	std::array<int8_t, 3> click_behavior{};
	uint8_t touch_behavior = 0;
	std::vector<std::pair<int32_t, int32_t>> pickup_handles, pickup_points;

	template <typename Archive>
	void serialize(Archive& ar) {
		ObjectV2::serialize(ar);
		ar(entity);
		ar(plane);
		for (auto& click : click_behavior)
			ar(click);
		ar(touch_behavior);
		for (auto* points : {&pickup_handles, &pickup_points}) {
			ar.size_u16(*points);
			for (auto& point : *points) {
				ar(point.first);
				ar(point.second);
			}
		}
	}
};

struct CompoundObjectV2 : ObjectV2 {
	struct Part {
		std::shared_ptr<EntityV2> entity;
		int32_t x = 0, y = 0;
	};
	std::vector<Part> parts;
	std::array<WinRect, 6> hotspots{};
	std::array<int32_t, 6> function_hotspots{};
	std::array<uint32_t, 6> function_messages{};
	std::array<uint8_t, 6> function_masks{};

	template <typename Archive>
	void serialize(Archive& ar) {
		ObjectV2::serialize(ar);
		ar.size_u32(parts);
		for (auto& part : parts) {
			ar(part.entity);
			ar(part.x);
			ar(part.y);
		}
		for (auto& hotspot : hotspots) {
			ar(hotspot.left);
			ar(hotspot.top);
			ar(hotspot.right);
			ar(hotspot.bottom);
		}
		for (auto& value : function_hotspots)
			ar(value);
		for (auto& value : function_messages)
			ar(value);
		ar(std::span<uint8_t>(function_masks));
	}
};

struct VehicleV2 : CompoundObjectV2 {
	int32_t velocity_x_256 = 0, velocity_y_256 = 0, x_256 = 0, y_256 = 0;
	WinRect cabin{};
	uint32_t bump = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		CompoundObjectV2::serialize(ar);
		ar(velocity_x_256);
		ar(velocity_y_256);
		ar(x_256);
		ar(y_256);
		ar(cabin.left);
		ar(cabin.top);
		ar(cabin.right);
		ar(cabin.bottom);
		ar(bump);
	}
};

struct LiftV2 : VehicleV2 {
	uint32_t num_floors = 0, current_floor = 0;
	int32_t current_call_button = -1;
	uint8_t delay = 0;
	std::array<int32_t, 8> floors{};
	std::array<ObjectV2*, 8> activated_buttons{};
	uint32_t align_with_cabin = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		VehicleV2::serialize(ar);
		ar(num_floors);
		ar(current_floor);
		ar(current_call_button);
		ar(delay);
		for (size_t i = 0; i < floors.size(); ++i) {
			ar(floors[i]);
			ar(activated_buttons[i]);
		}
		ar(align_with_cabin);
	}
};

struct CallButtonV2 : SimpleObjectV2 {
	std::shared_ptr<LiftV2> lift;
	uint8_t floor = 0;

	template <typename Archive>
	void serialize(Archive& ar) {
		SimpleObjectV2::serialize(ar);
		ar(lift);
		ar(floor);
	}
};

// C2 speech bubbles retain a 37-byte buffer, including bytes after its NUL.
struct BubbleV2 : SimpleObjectV2 {
	uint8_t life = 0;
	uint32_t creator_id = 0;
	std::array<uint8_t, 37> text{};
	std::array<uint8_t, 2> flags{};
	std::array<uint32_t, 2> position_state{};

	template <typename Archive>
	void serialize(Archive& ar) {
		SimpleObjectV2::serialize(ar);
		ar(life);
		ar(creator_id);
		ar(std::span<uint8_t>(text));
		ar(std::span<uint8_t>(flags));
		for (auto& value : position_state)
			ar(value);
	}
};

struct PointerToolV2 : SimpleObjectV2 {
	std::array<uint8_t, 51> pointer_data{};

	template <typename Archive>
	void serialize(Archive& ar) {
		SimpleObjectV2::serialize(ar);
		ar(std::span<uint8_t>(pointer_data));
	}
};

struct BlackboardV2 : CompoundObjectV2 {
	uint32_t background_color = 0, chalk_color = 0, alias_color = 0;
	uint8_t text_x = 0, text_y = 0;
	struct Word {
		uint32_t value = 0;
		std::array<uint8_t, 11> text{};
	};
	std::array<Word, 48> words{};

	template <typename Archive>
	void serialize(Archive& ar) {
		CompoundObjectV2::serialize(ar);
		ar(background_color);
		ar(chalk_color);
		ar(alias_color);
		ar(text_x);
		ar(text_y);
		for (auto& word : words) {
			ar(word.value);
			ar(std::span<uint8_t>(word.text));
		}
	}
};

struct SceneryV2 : ObjectV2 {
	std::shared_ptr<EntityV2> entity;

	template <typename Archive>
	void serialize(Archive& ar) {
		ObjectV2::serialize(ar);
		ar(entity);
	}
};

template <typename Archive>
void register_c2_object_classes(Archive& ar) {
	ar.template register_class<EntityV2>("Entity", 1);
	ar.template register_class<SimpleObjectV2>("SimpleObject", 2);
	ar.template register_class<BubbleV2>("Bubble", 2);
	ar.template register_class<CompoundObjectV2>("CompoundObject", 2);
	ar.template register_class<VehicleV2>("Vehicle", 2);
	ar.template register_class<LiftV2>("Lift", 2);
	ar.template register_class<CallButtonV2>("CallButton", 2);
	ar.template register_class<PointerToolV2>("PointerTool", 2);
	ar.template register_class<BlackboardV2>("Blackboard", 2);
	ar.template register_class<SceneryV2>("Scenery", 2);
}

} // namespace sfc

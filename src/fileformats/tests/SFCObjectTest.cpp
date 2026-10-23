#include "fileformats/sfc/C2MapData.h"
#include "fileformats/sfc/C2ObjectData.h"

#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"

#include <gtest/gtest.h>

static std::vector<uint8_t> encode_objects(std::vector<std::shared_ptr<sfc::ObjectV2>>& objects) {
	VectorWriter out;
	MFCWriter writer(out);
	sfc::register_c2_map_classes(writer);
	sfc::register_c2_object_classes(writer);
	writer.size_u32(objects);
	for (auto& object : objects)
		writer(object);
	return out.vector();
}

TEST(SFCObject, C2PreservesLinksVocabularyAnimationAndPhysics) {
	auto pointer = std::make_shared<sfc::PointerToolV2>();
	pointer->family = 2;
	pointer->genus = 1;
	pointer->species = 300;
	pointer->unid = 12345;
	pointer->attributes = 0x1234;
	pointer->timer_interval = 50;
	pointer->timer_state = 49;
	pointer->variables[99] = 0xdeadbeef;
	pointer->velocity_x = -12345;
	pointer->velocity_y = 23456;
	pointer->flags = 0x82;
	pointer->reserved_physics[5] = 255;
	pointer->current_sound = {0, 1, 2, 3};
	pointer->click_behavior = {0, -1, 2};
	pointer->touch_behavior = 64;
	pointer->pickup_handles = {{-1, -1}, {23, 45}};
	pointer->pickup_points = {{-3, 7}};
	pointer->pointer_data[50] = 99;
	pointer->entity = std::make_shared<sfc::EntityV2>();
	pointer->entity->has_animation = 1;
	pointer->entity->animation_frame = 8;
	pointer->entity->animation[0] = '0';
	pointer->entity->animation[98] = 255;
	pointer->entity->x = -40;
	pointer->entity->gallery = std::make_shared<sfc::CGalleryV1>();
	auto gallery = pointer->entity->gallery;
	gallery->filename = "hand";
	gallery->absolute_base = 0;
	gallery->refcount = 1;
	gallery->images.push_back({gallery.get(), 0, 20, 30, 0});
	pointer->gallery = gallery;
	pointer->scripts.push_back({1, 2, 9, 300, "outs [caf\xc3\xa9]"});

	auto board = std::make_shared<sfc::BlackboardV2>();
	board->carrier = pointer.get();
	pointer->object_pointer = board.get();
	board->parts = {{pointer->entity, 0, 0}, {nullptr, -15, 23}};
	board->function_messages[5] = 0x12345678;
	board->function_masks[5] = 0x80;
	board->words[47].value = 1000;
	board->words[47].text[0] = 'x';
	board->words[47].text[10] = 255;
	board->background_color = 0x123456;

	auto button = std::make_shared<sfc::CallButtonV2>();
	button->lift = std::make_shared<sfc::LiftV2>();
	button->floor = 7;
	button->lift->num_floors = 8;
	button->lift->floors[7] = -123;
	button->lift->activated_buttons[7] = button.get();
	button->lift->align_with_cabin = 1;
	button->lift->velocity_x_256 = -256;
	button->lift->bump = 1000;
	std::vector<std::shared_ptr<sfc::ObjectV2>> objects = {pointer, board, button};
	auto bytes = encode_objects(objects);

	SpanReader in(bytes);
	MFCReader reader(in);
	sfc::register_c2_map_classes(reader);
	sfc::register_c2_object_classes(reader);
	std::vector<std::shared_ptr<sfc::ObjectV2>> restored;
	reader.size_u32(restored);
	for (auto& object : restored)
		reader(object);
	ASSERT_EQ(restored.size(), 3u);
	auto restored_pointer = std::dynamic_pointer_cast<sfc::PointerToolV2>(restored[0]);
	auto restored_board = std::dynamic_pointer_cast<sfc::BlackboardV2>(restored[1]);
	auto restored_button = std::dynamic_pointer_cast<sfc::CallButtonV2>(restored[2]);
	ASSERT_NE(restored_pointer, nullptr);
	ASSERT_NE(restored_board, nullptr);
	ASSERT_NE(restored_button, nullptr);
	EXPECT_EQ(restored_pointer->object_pointer, restored_board.get());
	EXPECT_EQ(restored_board->carrier, restored_pointer.get());
	EXPECT_EQ(restored_pointer->variables[99], 0xdeadbeef);
	EXPECT_EQ(restored_pointer->velocity_x, -12345);
	EXPECT_EQ(restored_pointer->velocity_y, 23456);
	EXPECT_EQ(restored_pointer->timer_state, 49u);
	EXPECT_EQ(restored_pointer->species, 300);
	EXPECT_EQ(restored_pointer->flags, 0x82);
	EXPECT_EQ(restored_pointer->current_sound, pointer->current_sound);
	EXPECT_EQ(restored_pointer->pickup_handles, pointer->pickup_handles);
	EXPECT_EQ(restored_pointer->pickup_points, pointer->pickup_points);
	EXPECT_EQ(restored_pointer->entity->animation, pointer->entity->animation);
	EXPECT_EQ(restored_pointer->entity->animation_frame, 8);
	EXPECT_EQ(restored_pointer->gallery, restored_pointer->entity->gallery);
	ASSERT_EQ(restored_board->parts.size(), 2u);
	EXPECT_EQ(restored_board->parts[0].entity, restored_pointer->entity);
	EXPECT_EQ(restored_board->parts[1].entity, nullptr);
	EXPECT_EQ(restored_board->words[47].text, board->words[47].text);
	EXPECT_EQ(restored_board->words[47].value, 1000u);
	ASSERT_NE(restored_button->lift, nullptr);
	EXPECT_EQ(restored_button->lift->activated_buttons[7], restored_button.get());
	EXPECT_EQ(restored_button->lift->floors[7], -123);
	EXPECT_EQ(restored_button->lift->bump, 1000u);
	EXPECT_FALSE(in.has_data_left());
	EXPECT_EQ(encode_objects(restored), bytes);
}

TEST(SFCObject, C2RejectsTruncatedObjectState) {
	std::vector<std::shared_ptr<sfc::ObjectV2>> objects = {std::make_shared<sfc::PointerToolV2>()};
	auto bytes = encode_objects(objects);
	bytes.pop_back();
	SpanReader in(bytes);
	MFCReader reader(in);
	sfc::register_c2_map_classes(reader);
	sfc::register_c2_object_classes(reader);
	reader.size_u32(objects);
	EXPECT_ANY_THROW(reader(objects[0]));
}

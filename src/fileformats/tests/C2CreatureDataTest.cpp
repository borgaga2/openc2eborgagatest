#include "fileformats/sfc/C2CreatureData.h"
#include "fileformats/sfc/C2SFCFile.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"

#include <gtest/gtest.h>

TEST(C2CreatureData, PreservesSavedLearningFamilyBiologyAndWorldReferences) {
	sfc::C2SFCFile world;
	world.map = std::make_shared<sfc::MapDataV2>();
	auto creature = std::make_shared<sfc::CreatureV2>();
	creature->moniker = "test";
	creature->mother = "mum0";
	creature->father = "dad0";
	creature->parent_names = {"Mother", "Father"};
	creature->sex = 2;
	creature->age = 3;
	creature->age_ticks = 62980;
	creature->poses[255] = "252212212011232";
	creature->gait_animations[15] = "122R";
	creature->vocabulary[85] = {"food", "foo", 113};
	creature->speech_state[30] = 41;
	creature->physiological_state[82] = 7;
	creature->object_positions[39] = {-20, 44};
	creature->stimuli[45][11] = 19;
	creature->goals[39][16] = 91;
	creature->body = std::make_shared<sfc::BodyV2>();
	creature->body->has_animation = 1;
	creature->body->animation[98] = 0xa5;
	creature->limbs[0] = std::make_shared<sfc::LimbV2>();
	creature->limbs[0]->next = std::make_shared<sfc::LimbV2>();
	creature->limbs[0]->next->angle = -2;
	creature->brain = std::make_shared<sfc::CBrainV2>();
	creature->brain->ticks = 15734;
	creature->brain->lobes.resize(1);
	creature->brain->lobes[0].num_neurons = 1;
	creature->brain->neurons.resize(1);
	creature->brain->neurons[0].state[7] = 97;
	creature->brain->processing_order = {0};
	creature->biochemistry = std::make_shared<sfc::CBiochemistryV2>();
	creature->biochemistry->owner = creature.get();
	creature->biochemistry->reserved_after_organs = 112;
	creature->biochemistry->chemicals[90] = {143, 7};
	creature->biochemistry->organs[0] = creature->brain;
	creature->biochemistry->stored_organ_count = 1;
	auto instinct = std::make_shared<sfc::CInstinctV1>();
	instinct->dendrites[2] = {3, 4};
	instinct->reinforcement_amount = 64;
	creature->instincts = {instinct, instinct};
	creature->voices_lookup[2][26] = 8;
	creature->voices[31] = {{'t', 'e', 's', 't'}, 17};
	creature->history.monikers = {"test", "mum0", "dad0"};
	creature->history.names[0] = "caf\xc3\xa9";
	creature->history.text[5] = "Owner";
	creature->history.state[255] = 9;
	creature->history.reserved[8] = 0xfedcba98;
	auto bubble = std::make_shared<sfc::BubbleV2>();
	bubble->life = 15;
	bubble->creator_id = 42;
	bubble->text[0] = 'f'; bubble->text[1] = 0; bubble->text[36] = 0xff;
	bubble->flags = {1, 2};
	bubble->position_state = {50, 70};
	creature->object_references[2] = bubble.get();
	bubble->object_pointer = creature.get();
	world.objects = {creature, bubble};
	world.script_state.selected_creature = creature.get();
	world.script_state.speech_history = {"food", ""};
	world.script_state.place_history = {"The Incubator"};
	auto macro = std::make_shared<sfc::MacroV2>();
	macro->owner = creature.get(); macro->target = bubble.get();
	world.script_state.macros = {macro};
	VectorWriter encoded;
	sfc::write_c2_sfc_file(encoded, world);
	SpanReader input(encoded.vector());
	auto decoded_world = sfc::read_c2_sfc_file(input);
	ASSERT_EQ(decoded_world.objects.size(), 2u);
	auto decoded = std::dynamic_pointer_cast<sfc::CreatureV2>(decoded_world.objects[0]);
	auto decoded_bubble = std::dynamic_pointer_cast<sfc::BubbleV2>(decoded_world.objects[1]);
	ASSERT_TRUE(decoded); ASSERT_TRUE(decoded_bubble);
	EXPECT_EQ(decoded->parent_names, creature->parent_names);
	EXPECT_EQ(decoded->sex, 2); EXPECT_EQ(decoded->age, 3);
	EXPECT_EQ(decoded->age_ticks, 62980u);
	EXPECT_EQ(decoded->poses[255], creature->poses[255]);
	EXPECT_EQ(decoded->gait_animations[15], "122R");
	EXPECT_EQ(decoded->vocabulary[85].out, "foo");
	EXPECT_EQ(decoded->vocabulary[85].strength, 113u);
	EXPECT_EQ(decoded->physiological_state[82], 7);
	EXPECT_EQ(decoded->goals[39][16], 91u);
	ASSERT_TRUE(decoded->body);
	EXPECT_EQ(decoded->body->animation[98], 0xa5);
	ASSERT_TRUE(decoded->limbs[0]); ASSERT_TRUE(decoded->limbs[0]->next);
	EXPECT_EQ(decoded->limbs[0]->next->angle, -2);
	ASSERT_TRUE(decoded->brain); ASSERT_TRUE(decoded->biochemistry);
	ASSERT_EQ(decoded->brain->neurons.size(), 1u);
	EXPECT_EQ(decoded->brain->neurons[0].state[7], 97);
	EXPECT_EQ(decoded->biochemistry->owner, decoded.get());
	EXPECT_EQ(decoded->biochemistry->organs[0], decoded->brain);
	EXPECT_EQ(decoded->biochemistry->chemicals[90].concentration, 143);
	ASSERT_EQ(decoded->instincts.size(), 2u);
	EXPECT_EQ(decoded->instincts[0], decoded->instincts[1]);
	EXPECT_EQ(decoded->instincts[0]->reinforcement_amount, 64u);
	EXPECT_EQ(decoded->object_references[2], decoded_bubble.get());
	EXPECT_EQ(decoded_bubble->object_pointer, decoded.get());
	EXPECT_EQ(decoded_bubble->text, bubble->text);
	EXPECT_EQ(decoded_bubble->position_state, bubble->position_state);
	EXPECT_EQ(decoded->history.names[0], creature->history.names[0]);
	EXPECT_EQ(decoded->history.state[255], 9);
	EXPECT_EQ(decoded->history.reserved[8], 0xfedcba98u);
	EXPECT_EQ(decoded_world.script_state.selected_creature, decoded.get());
	ASSERT_EQ(decoded_world.script_state.macros.size(), 1u);
	EXPECT_EQ(decoded_world.script_state.macros[0]->owner, decoded.get());
	VectorWriter roundtrip;
	sfc::write_c2_sfc_file(roundtrip, decoded_world);
	EXPECT_EQ(roundtrip.vector(), encoded.vector());
}

TEST(C2CreatureData, RejectsTruncatedHistoryAndExcessiveInstincts) {
	auto creature = std::make_shared<sfc::CreatureV2>();
	VectorWriter output;
	MFCWriter writer(output);
	sfc::register_c2_creature_classes(writer);
	writer(creature);
	auto bytes = output.vector();
	bytes.pop_back();
	SpanReader input(bytes);
	MFCReader reader(input);
	sfc::register_c2_creature_classes(reader);
	std::shared_ptr<sfc::CreatureV2> decoded;
	EXPECT_THROW(reader(decoded), std::exception);
	creature->instincts.resize(21);
	VectorWriter invalid_output;
	MFCWriter invalid_writer(invalid_output);
	sfc::register_c2_creature_classes(invalid_writer);
	EXPECT_THROW(invalid_writer(creature), Exception);
}

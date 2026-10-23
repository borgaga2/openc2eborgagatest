#include "fileformats/sfc/C2MapData.h"
#include "fileformats/sfc/C2ScriptState.h"

#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"

#include <gtest/gtest.h>

TEST(SFCMacro, C2PreservesExecutionStateAndArchiveReferences) {
	auto owner = std::make_shared<sfc::SimpleObjectV2>();
	owner->unid = 100;
	auto target = std::make_shared<sfc::SimpleObjectV2>();
	target->unid = 200;
	auto macro = std::make_shared<sfc::MacroV2>();
	macro->self_destruct = 1;
	macro->instant = 1;
	macro->stored_script_length = 42;
	macro->script = "setv var0 3,wait 10,stop";
	macro->instruction_pointer = 19;
	macro->stack[0] = 12;
	macro->stack[19] = -123456;
	macro->stack_pointer = 1;
	macro->variables[0] = 3;
	macro->variables[99] = -1000;
	macro->owner = owner.get();
	macro->from = target.get();
	macro->exec = owner.get();
	macro->target = target.get();
	macro->it = target.get();
	macro->part = 4;
	macro->subroutine_label = {'t', 'e', 's', 't'};
	macro->subroutine_address = 8;
	macro->wait = 7;
	macro->reserved[15] = 255;
	sfc::ScriptStateV2 state;
	state.scroll_x = -20;
	state.scroll_y = 400;
	state.selected_creature = owner.get();
	state.favorite_places[5] = {"caf\xc3\xa9", -10, 1000};
	state.reserved_after_places = 2;
	state.speech_history = {"hello", "caf\xc3\xa9"};
	state.macros = {macro, nullptr, macro};

	VectorWriter out;
	MFCWriter writer(out);
	sfc::register_c2_map_classes(writer);
	sfc::register_c2_object_classes(writer);
	sfc::register_c2_macro_class(writer);
	writer(owner);
	writer(target);
	state.serialize(writer);

	SpanReader in(out.vector());
	MFCReader reader(in);
	sfc::register_c2_map_classes(reader);
	sfc::register_c2_object_classes(reader);
	sfc::register_c2_macro_class(reader);
	std::shared_ptr<sfc::SimpleObjectV2> restored_owner, restored_target;
	reader(restored_owner);
	reader(restored_target);
	sfc::ScriptStateV2 restored;
	restored.serialize(reader);
	ASSERT_EQ(restored.macros.size(), 3u);
	ASSERT_NE(restored.macros[0], nullptr);
	EXPECT_EQ(restored.macros[0], restored.macros[2]);
	EXPECT_EQ(restored.macros[1], nullptr);
	const auto& running = *restored.macros[0];
	EXPECT_EQ(running.instruction_pointer, 19u);
	EXPECT_EQ(running.stack_pointer, 1u);
	EXPECT_EQ(running.stack, macro->stack);
	EXPECT_EQ(running.variables, macro->variables);
	EXPECT_EQ(running.owner, restored_owner.get());
	EXPECT_EQ(running.from, restored_target.get());
	EXPECT_EQ(running.exec, restored_owner.get());
	EXPECT_EQ(running.target, restored_target.get());
	EXPECT_EQ(running.it, restored_target.get());
	EXPECT_EQ(running.wait, 7);
	EXPECT_EQ(running.part, 4);
	EXPECT_EQ(running.stored_script_length, 42u);
	EXPECT_EQ(running.script, macro->script);
	EXPECT_EQ(running.reserved, macro->reserved);
	EXPECT_EQ(running.subroutine_label, macro->subroutine_label);
	EXPECT_EQ(running.subroutine_address, 8u);
	EXPECT_EQ(restored.selected_creature, restored_owner.get());
	EXPECT_EQ(restored.scroll_x, -20);
	EXPECT_EQ(restored.scroll_y, 400);
	EXPECT_EQ(restored.reserved_after_places, 2u);
	EXPECT_EQ(restored.favorite_places[5].name, state.favorite_places[5].name);
	EXPECT_EQ(restored.favorite_places[5].x, -10);
	EXPECT_EQ(restored.speech_history, state.speech_history);
	EXPECT_FALSE(in.has_data_left());

	VectorWriter reencoded;
	MFCWriter second_writer(reencoded);
	sfc::register_c2_map_classes(second_writer);
	sfc::register_c2_object_classes(second_writer);
	sfc::register_c2_macro_class(second_writer);
	second_writer(restored_owner);
	second_writer(restored_target);
	restored.serialize(second_writer);
	EXPECT_EQ(reencoded.vector(), out.vector());
}

TEST(SFCMacro, C2RejectsTruncatedExecutionState) {
	auto macro = std::make_shared<sfc::MacroV2>();
	VectorWriter out;
	MFCWriter writer(out);
	sfc::register_c2_macro_class(writer);
	writer(macro);
	auto bytes = out.vector();
	bytes.pop_back();
	SpanReader in(bytes);
	MFCReader reader(in);
	sfc::register_c2_macro_class(reader);
	std::shared_ptr<sfc::MacroV2> restored;
	EXPECT_ANY_THROW(reader(restored));
}

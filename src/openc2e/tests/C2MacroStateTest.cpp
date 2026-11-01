#include "openc2e/C2MacroState.h"
#include "openc2e/Agent.h"
#include "openc2e/Engine.h"
#include "openc2e/World.h"
#include "openc2e/caosScript.h"
#include "openc2e/caosVM.h"
#include "fileformats/sfc/C2ScriptState.h"
#include "common/Exception.h"
#include "common/encoding.h"
#include <gtest/gtest.h>

namespace {
class MacroAgent : public Agent {
  public:
	MacroAgent() : Agent(2, 1, 1, 1000) {}
	CompoundPart* part(unsigned int) override { return nullptr; }
};
class C2MacroState : public testing::Test {
  protected:
	int previous_version = engine.version;
	unsigned int previous_tick = world.tickcount;
	sfc::SimpleObjectV2 original;
	std::shared_ptr<MacroAgent> agent = std::make_shared<MacroAgent>();
	void SetUp() override { engine.version = 2; world.tickcount = 100; }
	void TearDown() override { engine.version = previous_version; world.tickcount = previous_tick; }
	sfc::MacroV2 macro(const std::string& source, const std::string& current) {
		sfc::MacroV2 saved;
		saved.self_destruct = 1;
		saved.reserved[8] = 1; // interruptible
		saved.owner = saved.target = &original;
		saved.script = source;
		saved.instruction_pointer = utf8_to_cp1252(source.substr(0, source.find(current))).size();
		return saved;
	}
	std::unique_ptr<caosVM> restore(const sfc::MacroV2& saved) {
		return prepareC2Macro(saved, [&](const sfc::ObjectV2* object) {
			return object == &original ? AgentRef(agent.get()) : AgentRef();
		});
	}
	void tick(caosVM& vm) { ++world.tickcount; vm.timeslice = 1; vm.tick(); }
	void finish(caosVM& vm) {
		for (unsigned int i = 0; i < 100 && !vm.stopped(); ++i) tick(vm);
		ASSERT_TRUE(vm.stopped());
	}
};
}

TEST_F(C2MacroState, ResumesRemainingWaitAndLocalsWithoutReplayingEarlierCommands) {
	auto saved = macro("setv ov00 999,wait 50,addv ov00 var7,endm", "wait");
	saved.wait = 2;
	saved.variables[7] = 3;
	saved.from = saved.it = &original;
	saved.part = 4;
	agent->var[0].setInt(40);
	auto vm = restore(saved);
	EXPECT_EQ(vm->from.getAgent(), agent);
	EXPECT_EQ(vm->_it_.get(), agent.get());
	EXPECT_EQ(vm->part, 4);
	EXPECT_EQ(agent->var[0].getInt(), 40);
	tick(*vm);
	EXPECT_TRUE(vm->isBlocking());
	tick(*vm);
	EXPECT_FALSE(vm->isBlocking());
	EXPECT_EQ(agent->var[0].getInt(), 40);
	finish(*vm);
	EXPECT_EQ(agent->var[0].getInt(), 43);
}

TEST_F(C2MacroState, WaitWithZeroCounterStartsTheOriginalWait) {
	auto saved = macro("wait 5,setv ov00 7,endm", "wait");
	auto vm = restore(saved);
	tick(*vm);
	for (int i = 0; i < 4; ++i) tick(*vm);
	EXPECT_EQ(agent->var[0].getInt(), 0);
	EXPECT_TRUE(vm->isBlocking());
	finish(*vm);
	EXPECT_EQ(agent->var[0].getInt(), 7);
}

TEST_F(C2MacroState, RestoresSubroutineReturnAcrossConditionalBranches) {
	auto saved = macro("setv ov00 99,doif 1 eq 1,gsub work,else,setv ov00 200,endi,addv ov00 1,endm,subr work,wait 20,addv ov00 var8,retn", "wait");
	saved.wait = 1;
	saved.variables[8] = 7;
	saved.stack_pointer = 1;
	saved.stack[0] = saved.script.find("else");
	agent->var[0].setInt(10);
	auto vm = restore(saved);
	ASSERT_EQ(vm->callStack.size(), 1u);
	finish(*vm);
	EXPECT_EQ(agent->var[0].getInt(), 18);
}

TEST_F(C2MacroState, RestoresLoopWithoutRestartingItsCompletedIterations) {
	auto saved = macro("setv var7 0,loop,addv var7 1,wait 8,untl var7 eq 5,setv ov00 var7,endm", "wait");
	saved.wait = 1;
	saved.variables[7] = 4;
	saved.stack_pointer = 1;
	saved.stack[0] = saved.script.find("addv");
	auto vm = restore(saved);
	EXPECT_TRUE(vm->callStack.empty());
	finish(*vm);
	EXPECT_EQ(agent->var[0].getInt(), 5);
}

TEST_F(C2MacroState, NativeByteOffsetsSurviveUtf8Conversion) {
	auto saved = macro("dbgm [caf\xc3\xa9],setv ov00 999,wait 10,setv ov00 7,endm", "wait");
	saved.wait = 1;
	auto vm = restore(saved);
	finish(*vm);
	EXPECT_EQ(agent->var[0].getInt(), 7);
}

TEST_F(C2MacroState, InvalidOffsetsStacksAndReferencesFailBeforeExecutingAnything) {
	auto saved = macro("setv ov00 999,wait 10,endm", "wait");
	++saved.instruction_pointer;
	EXPECT_THROW(restore(saved), Exception);
	--saved.instruction_pointer;
	saved.stack_pointer = 1;
	saved.stack[0] = 3;
	EXPECT_THROW(restore(saved), Exception);
	saved.stack_pointer = 0;
	sfc::SimpleObjectV2 missing;
	saved.it = &missing;
	EXPECT_THROW(restore(saved), Exception);
	EXPECT_EQ(agent->var[0].getInt(), 0);
}

TEST_F(C2MacroState, PreservesInstantAndLockedState) {
	auto saved = macro("wait 10,endm", "wait");
	saved.instant = 65535;
	saved.reserved[8] = 0;
	auto vm = restore(saved);
	EXPECT_TRUE(vm->inst);
	EXPECT_TRUE(vm->lock);
	caosScript replacement("c2", "test");
	replacement.parse("setv ov00 9,endm");
	EXPECT_FALSE(vm->fireScript(replacement.installer, false));
}

TEST_F(C2MacroState, RestoresRepetitionCounterAcrossSubroutineCall) {
	auto saved = macro("reps 10,addv ov00 1,gsub work,repe,endm,subr work,wait 20,addv ov01 1,retn", "wait");
	saved.wait = 1;
	saved.stack_pointer = 3;
	saved.stack[0] = saved.script.find("addv ov00");
	saved.stack[1] = 2;
	saved.stack[2] = saved.script.find("repe");
	agent->var[0].setInt(9);
	agent->var[1].setInt(8);
	auto vm = restore(saved);
	ASSERT_EQ(vm->callStack.size(), 1u);
	ASSERT_EQ(vm->callStack[0].valueStack.size(), 1u);
	EXPECT_EQ(vm->callStack[0].valueStack[0].getInt(), 2);
	finish(*vm);
	EXPECT_EQ(agent->var[0].getInt(), 10);
	EXPECT_EQ(agent->var[1].getInt(), 10);
}

TEST_F(C2MacroState, RestoresNestedRepetitionsWithoutReevaluatingTheirCounts) {
	auto saved = macro("reps 10,reps 20,wait 1,addv ov00 1,repe,addv ov01 1,repe,endm", "wait");
	saved.wait = 1;
	saved.stack_pointer = 4;
	saved.stack[0] = saved.script.find("reps 20");
	saved.stack[1] = 1;
	saved.stack[2] = saved.script.find("wait");
	saved.stack[3] = 2;
	auto vm = restore(saved);
	finish(*vm);
	EXPECT_EQ(agent->var[0].getInt(), 2);
	EXPECT_EQ(agent->var[1].getInt(), 1);
	saved.stack_pointer = 3;
	EXPECT_THROW(restore(saved), Exception);
}

TEST_F(C2MacroState, NonpositiveSavedRepetitionFinishesCurrentBodyOnce) {
	for (int count : {0, -7}) {
		auto saved = macro("reps 0,wait 10,addv ov00 1,repe,endm", "wait");
		saved.wait = 1;
		saved.stack_pointer = 2;
		saved.stack[0] = saved.script.find("wait");
		saved.stack[1] = count;
		agent->var[0].setInt(0);
		auto vm = restore(saved);
		finish(*vm);
		EXPECT_EQ(agent->var[0].getInt(), 1);
	}
}

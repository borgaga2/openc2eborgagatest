#include "C2MacroState.h"
#include "caosScript.h"
#include "caosVM.h"
#include "common/Exception.h"
#include "common/encoding.h"
#include "fileformats/sfc/C2ScriptState.h"
#include <iterator>
#include <algorithm>
#include <limits>

void c_WAIT(caosVM*);

std::unique_ptr<caosVM> prepareC2Macro(const sfc::MacroV2& saved,
	const std::function<AgentRef(const sfc::ObjectV2*)>& resolve) {
	if (!saved.owner || saved.exec || saved.self_destruct != 1 || saved.stack_pointer > saved.stack.size() || saved.wait < 0)
		throw Exception("Unsupported C2 macro ownership or execution state");
	const auto native_source = utf8_to_cp1252(saved.script);
	const auto source_offset = [&](uint32_t offset) {
		if (offset > native_source.size()) throw Exception("C2 macro source offset is out of range");
		return cp1252_to_utf8(native_source.substr(0, offset)).size();
	};
	caosScript compiled("c2", "<saved C2 macro>");
	compiled.parse(saved.script, true);
	if (!compiled.scripts.empty() || compiled.removal)
		throw Exception("C2 macro contains script installation sections");
	const auto code = compiled.installer;
	const auto offset = source_offset(saved.instruction_pointer);
	const auto command = code->source_commands.find(offset);
	if (command == code->source_commands.end())
		throw Exception("C2 macro instruction pointer is not at a command boundary");
	const auto resolve_checked = [&](const sfc::ObjectV2* object) {
		if (!object) return AgentRef();
		auto agent = resolve(object);
		if (!agent) throw Exception("C2 macro refers to a missing agent");
		return agent;
	};
	auto vm = std::make_unique<caosVM>(resolve_checked(saved.owner));
	vm->currentscript = code;
	vm->nip = command->second.instruction;
	vm->targ = resolve_checked(saved.target);
	vm->from.setAgent(resolve_checked(saved.from));
	vm->_it_ = resolve_checked(saved.it);
	vm->part = saved.part;
	vm->inst = saved.instant != 0;
	uint32_t interruptible = 0;
	for (unsigned i = 0; i < 4; ++i) interruptible |= uint32_t(saved.reserved[8 + i]) << (8 * i);
	if (interruptible > 1) throw Exception("Invalid saved C2 macro interruptibility");
	vm->lock = interruptible == 0;
	for (unsigned i = 0; i < saved.variables.size(); ++i)
		vm->var[code->mapVAxx(i)].setInt(saved.variables[i]);
	for (unsigned i = 0; i < saved.stack_pointer; ++i) {
		if (saved.stack[i] < 0) throw Exception("Unsupported C2 macro stack value");
		const auto address = source_offset(saved.stack[i]);
		const auto call = code->source_gsub_returns.find(address);
		if (call != code->source_gsub_returns.end()) {
			vm->callStack.push_back({{}, call->second});
			vm->callStack.back().valueStack.swap(vm->valueStack);
			continue;
		}
		const auto repeat = code->source_reps_bodies.find(address);
		if (repeat != code->source_reps_bodies.end()) {
			// Native REPS pushes the body address followed by the remaining
			// iteration count. Our bytecode embeds the address and retains only
			// the count on the value stack (saved separately across GSUB).
			if (i + 1 >= saved.stack_pointer || saved.stack[i + 1] == std::numeric_limits<int32_t>::min())
				throw Exception("Unsupported C2 repetition counter");
			bool active = offset >= address && offset <= repeat->second;
			for (unsigned j = i + 2; j < saved.stack_pointer && !active; ++j) {
				if (saved.stack[j] < 0 || static_cast<size_t>(saved.stack[j]) > native_source.size()) continue;
				const auto nested = source_offset(saved.stack[j]);
				active = nested >= address && nested <= repeat->second;
			}
			if (!active) throw Exception("Inactive saved C2 repetition frame");
			// Native REPE exits when the decremented count is <= 0.
			// DECJNZ exits only at zero, so normalize an already nonpositive
			// count to one while retaining the suspended body position.
			vm->valueStack.push_back(caosValue(std::max(1, saved.stack[++i])));
			continue;
		}
		const auto loop = code->source_loop_bodies.find(address);
		if (loop != code->source_loop_bodies.end()) {
			// LOOP destinations are encoded in our bytecode. Check that this
			// frame encloses the current instruction or a suspended call.
			bool active = offset >= address && offset <= loop->second;
			for (unsigned j = i + 1; j < saved.stack_pointer && !active; ++j) {
				const auto nested = source_offset(saved.stack[j]);
				active = nested >= address && nested <= loop->second;
			}
			if (active) continue;
		}
		throw Exception("Unsupported C2 macro stack frame (enumeration restoration remains unfinished)");
	}
	if (saved.wait > 0) {
		if (command->second.name != "wait") throw Exception("C2 macro wait count belongs to a non-WAIT command");
		const auto next = std::next(command);
		const int end = next == code->source_commands.end() ? code->scriptLength() : next->second.instruction;
		if (end <= vm->nip || code->getOp(end - 1).opcode != CAOS_YIELD)
			throw Exception("C2 WAIT has no resumable yield boundary");
		// Native C2 keeps IP on WAIT while its separate counter counts down.
		// Skip argument evaluation and retain the command's final time slice.
		vm->nip = end - 1;
		vm->valueStack.push_back(caosValue(saved.wait));
		c_WAIT(vm.get());
	}
	vm->timeslice = 0;
	return vm;
}

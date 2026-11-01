#pragma once

#include "AgentRef.h"
#include <functional>
#include <memory>

class caosVM;
namespace sfc { struct MacroV2; struct ObjectV2; }

// Prepare a suspended VM without running any command or attaching it to an
// agent. Unsupported native stack states fail explicitly.
std::unique_ptr<caosVM> prepareC2Macro(const sfc::MacroV2&,
	const std::function<AgentRef(const sfc::ObjectV2*)>& resolve);

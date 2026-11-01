#pragma once

#include "AgentRef.h"
#include <functional>
#include <map>
#include <memory>
#include <string>

class creaturesImage;
class genomeFile;
namespace sfc { struct C2SFCFile; struct ObjectV2; }

struct C2WorldAssets {
	std::function<std::shared_ptr<genomeFile>(const std::string&)> genome;
	std::function<std::shared_ptr<creaturesImage>(const std::string&)> composite;
};

// Construct the archived world in an initialized, empty C2 engine. Keep it
// paused: suspended macros are attached without execution. Lifecycle, native
// world state and failure recovery still need work before this can replace
// normal saved-game loading. Reference and creature-asset checks run before
// world construction, but later failures do not yet roll back the world.
std::map<const sfc::ObjectV2*, AgentRef> loadC2WorldSnapshot(
	const sfc::C2SFCFile&, const C2WorldAssets&);

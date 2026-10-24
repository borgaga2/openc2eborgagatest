#pragma once

#include "C2MapData.h"
#include "C2ObjectData.h"
#include "C2ScriptState.h"

#include <filesystem>
#include <memory>
#include <vector>

class Reader;
class Writer;

namespace sfc {

// The sections understood by the C2 world archive reader. The bytes between
// the map and object count, and after the running macro table, are retained
// verbatim until their meaning is known.
struct C2SFCFile {
	std::shared_ptr<MapDataV2> map;
	std::vector<uint8_t> map_separator;
	std::vector<std::shared_ptr<ObjectV2>> objects;
	std::vector<std::shared_ptr<SceneryV2>> sceneries;
	std::vector<ScriptV2> scripts;
	ScriptStateV2 script_state;
	std::vector<uint8_t> trailing_data;
};

C2SFCFile read_c2_sfc_file(Reader& in);
C2SFCFile read_c2_sfc_file(const std::filesystem::path& path);
void write_c2_sfc_file(Writer& out, C2SFCFile& file);
void write_c2_sfc_file(const std::filesystem::path& path, C2SFCFile& file);

} // namespace sfc


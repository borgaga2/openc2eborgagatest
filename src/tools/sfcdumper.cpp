#include "common/io/FileReader.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"
#include "fileformats/sfc/C2MapData.h"
#include "fileformats/sfc/C2ObjectData.h"
#include "fileformats/sfc/C2ScriptState.h"
#include "fileformats/sfc/SFCFile.h"

#include <algorithm>
#include <fmt/core.h>

enum class C2Section { Map, Objects, Macros };

static int check_c2_archive(const char* filename, C2Section section) {
	FileReader file(filename);
	auto bytes = file.read_to_end();
	SpanReader in(bytes);
	MFCReader reader(in);
	sfc::register_c2_map_classes(reader);
	sfc::register_c2_object_classes(reader);
	sfc::register_c2_macro_class(reader);
	std::shared_ptr<sfc::MapDataV2> map;
	reader(map);
	if (!map)
		throw Exception("Missing C2 map");
	VectorWriter out;
	MFCWriter writer(out);
	sfc::register_c2_map_classes(writer);
	sfc::register_c2_object_classes(writer);
	sfc::register_c2_macro_class(writer);
	writer(map);
	if (section != C2Section::Map) {
		// The legacy loader skips this zero-filled region after the map.
		// Preserve it verbatim while checking the following object section;
		// its interpretation is still needed for full world serialization.
		std::vector<uint8_t> separator;
		while (in.peek_byte() == 0) {
			separator.push_back(0);
			in.seek_relative(1);
		}
		// A count divisible by 256 starts with zero too. Locate the first
		// object's new-class marker instead of swallowing count bytes as padding.
		bool found_count = false;
		for (size_t rewind = 0; rewind <= 3 && rewind <= separator.size(); ++rewind) {
			const size_t marker = in.tell() - rewind + 4;
			if (marker + 3 < bytes.size() && bytes[marker] == 0xff && bytes[marker + 1] == 0xff && bytes[marker + 2] == 2 && bytes[marker + 3] == 0) {
				separator.resize(separator.size() - rewind);
				in.seek_relative(-static_cast<int64_t>(rewind));
				found_count = true;
				break;
			}
		}
		if (!found_count)
			throw Exception("Cannot locate the C2 object section after the map");
		out.write(reinterpret_cast<const char*>(separator.data()), separator.size());
		std::vector<std::shared_ptr<sfc::ObjectV2>> objects;
		reader.size_u32(objects);
		for (auto& object : objects)
			reader(object);
		writer.size_u32(objects);
		for (auto& object : objects)
			writer(object);
		std::vector<std::shared_ptr<sfc::SceneryV2>> sceneries;
		reader.size_u32(sceneries);
		for (auto& scenery : sceneries)
			reader(scenery);
		writer.size_u32(sceneries);
		for (auto& scenery : sceneries)
			writer(scenery);
		std::vector<sfc::ScriptV2> scripts;
		reader.size_u32(scripts);
		for (auto& script : scripts)
			script.serialize(reader);
		writer.size_u32(scripts);
		for (auto& script : scripts)
			script.serialize(writer);
		fmt::print("C2 objects: {}, sceneries: {}, scripts: {}\n", objects.size(), sceneries.size(), scripts.size());
	}
	if (section == C2Section::Macros) {
		sfc::ScriptStateV2 state;
		state.serialize(reader);
		state.serialize(writer);
		const auto active = std::count_if(state.macros.begin(), state.macros.end(), [](const auto& macro) { return macro != nullptr; });
		fmt::print("C2 running scripts: {}, macro slots: {}, speech entries: {}\n", active, state.macros.size(), state.speech_history.size());
		fmt::print("Remaining bytes after macros: {}\n", bytes.size() - in.tell());
	}
	const auto& encoded = out.vector();
	const bool exact = encoded.size() == in.tell() && std::equal(encoded.begin(), encoded.end(), bytes.begin());
	const char* label = section == C2Section::Map ? "map" : section == C2Section::Objects ? "map and objects" : "map, objects and macros";
	fmt::print("C2 {}: {} room slots, {} bytes; exact read/write round trip: {}\n", label, map->rooms.size(), in.tell(), exact);
	return exact ? 0 : 1;
}

int main(int argc, char** argv) try {
	if (argc == 3 && strcmp(argv[1], "--c2-map") == 0)
		return check_c2_archive(argv[2], C2Section::Map);
	if (argc == 3 && strcmp(argv[1], "--c2-objects") == 0)
		return check_c2_archive(argv[2], C2Section::Objects);
	if (argc == 3 && strcmp(argv[1], "--c2-macros") == 0)
		return check_c2_archive(argv[2], C2Section::Macros);
	if (argc != 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		fmt::print(stderr, "USAGE: {} FILE\n       {} --c2-map FILE\n       {} --c2-objects FILE\n       {} --c2-macros FILE\n", argv[0], argv[0], argv[0], argv[0]);
		return 1;
	}

	auto input_filename = argv[1];

	FileReader in(input_filename);
	auto sfc = sfc::read_sfc_v1_file(in);

	// check if we read all of it
	fmt::print("read entire file? {}\n", !in.has_data_left());

	fmt::print("number of objects {}\n", sfc.objects.size());
	fmt::print("number of sceneries {}\n", sfc.sceneries.size());
	fmt::print("number of scripts {}\n", sfc.scripts.size());
	fmt::print("number of macros {}\n", sfc.macros.size());
} catch (const std::exception& error) {
	fmt::print(stderr, "Error: {}\n", error.what());
	return 1;
}

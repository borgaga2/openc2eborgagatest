#include "common/io/FileReader.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/sfc/C2SFCFile.h"
#include "fileformats/sfc/SFCFile.h"

#include <algorithm>
#include <fmt/core.h>

enum class C2Section { Map, Objects, Macros };

static int check_c2_archive(const char* filename, C2Section section) {
	FileReader file(filename);
	auto bytes = file.read_to_end();
	SpanReader in(bytes);
	auto archive = sfc::read_c2_sfc_file(in);
	VectorWriter out;
	sfc::write_c2_sfc_file(out, archive);
	if (section != C2Section::Map) {
		fmt::print("C2 objects: {}, sceneries: {}, scripts: {}\n", archive.objects.size(), archive.sceneries.size(), archive.scripts.size());
	}
	if (section == C2Section::Macros) {
		const auto active = std::count_if(archive.script_state.macros.begin(), archive.script_state.macros.end(), [](const auto& macro) { return macro != nullptr; });
		fmt::print("C2 running scripts: {}, macro slots: {}, speech entries: {}\n", active, archive.script_state.macros.size(), archive.script_state.speech_history.size());
		fmt::print("Remaining bytes after macros: {}\n", archive.trailing_data.size());
	}
	const auto& encoded = out.vector();
	const bool exact = encoded == bytes;
	const char* label = section == C2Section::Map ? "map" : section == C2Section::Objects ? "map and objects" : "map, objects and macros";
	fmt::print("C2 {}: {} room slots, {} bytes; exact read/write round trip: {}\n", label, archive.map->rooms.size(), encoded.size(), exact);
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


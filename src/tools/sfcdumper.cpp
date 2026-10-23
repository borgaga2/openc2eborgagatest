#include "common/io/FileReader.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "fileformats/MFCWriter.h"
#include "fileformats/sfc/C2MapData.h"
#include "fileformats/sfc/SFCFile.h"

#include <algorithm>
#include <fmt/core.h>

static int check_c2_map(const char* filename) {
	FileReader file(filename);
	auto bytes = file.read_to_end();
	SpanReader in(bytes);
	MFCReader reader(in);
	sfc::register_c2_map_classes(reader);
	std::shared_ptr<sfc::MapDataV2> map;
	reader(map);
	if (!map)
		throw Exception("Missing C2 map");
	VectorWriter out;
	MFCWriter writer(out);
	sfc::register_c2_map_classes(writer);
	writer(map);
	const auto& encoded = out.vector();
	const bool exact = encoded.size() == in.tell() && std::equal(encoded.begin(), encoded.end(), bytes.begin());
	fmt::print("C2 map: {} room slots, {} bytes; exact read/write round trip: {}\n", map->rooms.size(), in.tell(), exact);
	return exact ? 0 : 1;
}

int main(int argc, char** argv) try {
	if (argc == 3 && strcmp(argv[1], "--c2-map") == 0)
		return check_c2_map(argv[2]);
	if (argc != 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		fmt::print(stderr, "USAGE: {} FILE\n       {} --c2-map FILE\n", argv[0], argv[0]);
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

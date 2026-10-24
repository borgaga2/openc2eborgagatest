#include "C2SFCFile.h"

#include "common/Exception.h"
#include "common/io/FileReader.h"
#include "common/io/FileWriter.h"
#include "common/io/SpanReader.h"
#include "fileformats/MFCReader.h"
#include "fileformats/MFCWriter.h"

#include <algorithm>
#include <cstdint>

namespace sfc {

namespace {

void register_c2_classes(MFCReader& ar) {
	register_c2_map_classes(ar);
	register_c2_object_classes(ar);
	register_c2_macro_class(ar);
}

void register_c2_classes(MFCWriter& ar) {
	register_c2_map_classes(ar);
	register_c2_object_classes(ar);
	register_c2_macro_class(ar);
}

void serialize_sections(MFCReader& ar, SpanReader& stream, const std::vector<uint8_t>& bytes, C2SFCFile& file) {
	ar(file.map);
	if (!file.map)
		throw Exception("Missing C2 map in world archive");

	// C2 inserts a zero-filled gap before the object count. A count divisible
	// by 256 also starts with zero, so use the first-object marker to distinguish
	// the count from padding, matching the proven legacy-loader boundary rule.
	while (stream.has_data_left() && stream.peek_byte() == 0) {
		file.map_separator.push_back(0);
		stream.seek_relative(1);
	}
	const size_t after_padding = stream.tell();
	bool found_count = false;
	for (size_t rewind = 0; rewind <= 3 && rewind <= file.map_separator.size(); ++rewind) {
		const size_t count_start = after_padding - rewind;
		const size_t marker = count_start + 4;
		if (marker + 3 < bytes.size() && bytes[marker] == 0xff && bytes[marker + 1] == 0xff &&
			bytes[marker + 2] == 2 && bytes[marker + 3] == 0) {
			file.map_separator.resize(file.map_separator.size() - rewind);
			stream.seek_relative(-static_cast<int64_t>(rewind));
			found_count = true;
			break;
		}
	}
	if (!found_count)
		throw Exception("Cannot locate the C2 object section after the map");

	ar.size_u32(file.objects);
	for (auto& object : file.objects)
		ar(object);
	ar.size_u32(file.sceneries);
	for (auto& scenery : file.sceneries)
		ar(scenery);
	ar.size_u32(file.scripts);
	for (auto& script : file.scripts)
		script.serialize(ar);
	file.script_state.serialize(ar);
}

} // namespace

C2SFCFile read_c2_sfc_file(const std::filesystem::path& path) {
	FileReader in(path);
	return read_c2_sfc_file(in);
}

C2SFCFile read_c2_sfc_file(Reader& in) {
	auto bytes = in.read_to_end();
	SpanReader stream(bytes);
	MFCReader reader(stream);
	register_c2_classes(reader);

	C2SFCFile file;
	serialize_sections(reader, stream, bytes, file);
	file.trailing_data = stream.read_to_end();
	return file;
}

void write_c2_sfc_file(const std::filesystem::path& path, C2SFCFile& file) {
	FileWriter out(path);
	write_c2_sfc_file(out, file);
}

void write_c2_sfc_file(Writer& out, C2SFCFile& file) {
	MFCWriter writer(out);
	register_c2_classes(writer);
	writer(file.map);
	out.write(file.map_separator);
	writer.size_u32(file.objects);
	for (auto& object : file.objects)
		writer(object);
	writer.size_u32(file.sceneries);
	for (auto& scenery : file.sceneries)
		writer(scenery);
	writer.size_u32(file.scripts);
	for (auto& script : file.scripts)
		script.serialize(writer);
	file.script_state.serialize(writer);
	out.write(file.trailing_data);
}

} // namespace sfc


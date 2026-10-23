#pragma once

#include <stdint.h>
#include <string>
#include <vector>

class Reader;

// C2 hatchery egg container. Parent genomes remain separate: crossing and
// mutation belong to the engine, not the file reader.
struct C2EggFile {
	uint8_t gender = 0;
	uint8_t image = 0;
	std::string mother_moniker;
	std::string father_moniker;
	std::string description;
	std::vector<uint8_t> mother_genome;
	std::vector<uint8_t> father_genome;

	static C2EggFile read(Reader&);
};

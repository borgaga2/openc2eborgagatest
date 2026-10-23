#include "common/Exception.h"
#include "common/Ascii.h"
#include "common/io/FileReader.h"
#include "fileformats/C2EggFile.h"
#include "openc2e/Engine.h"
#include "openc2e/Map.h"
#include "openc2e/PathResolver.h"
#include "openc2e/Room.h"
#include "openc2e/World.h"
#include "openc2e/creatures/CreatureAgent.h"
#include "openc2e/creatures/oldBrain.h"
#include "openc2e/creatures/oldCreature.h"
#include "openc2e/historyManager.h"
#include "sdlbackend/SDLMixerBackend.h"

#include <array>
#include <filesystem>
#include <fmt/core.h>
#include <string>

// An opt-in integration check against a user's installed C2 assets. It runs
// the real world, scripts, skeleton and biochemistry with the null renderer.
int main(int argc, char** argv) {
	if (argc != 2) {
		fmt::print(stderr, "USAGE: {} CREATURES_2_DATA_DIRECTORY\n", argv[0]);
		return 1;
	}
	bool initialized = false;
	try {
		engine.addPossibleAudioBackend("sdlmixer", SDLMixerBackend::get_instance());
		std::array<std::string, 7> args = {argv[0], "--backend", "null", "--audiobackend", "sdlmixer", "--data-path", argv[1]};
		std::array<char*, 7> pointers;
		for (size_t i = 0; i < args.size(); ++i)
			pointers[i] = args[i].data();
		if (!engine.parseCommandLine(static_cast<int>(pointers.size()), pointers.data()) || !engine.initialSetup())
			throw Exception("Could not initialize the C2 integration world");
		initialized = true;
		if (engine.gametype != "c2")
			throw Exception("The data directory must contain Creatures 2");
		const auto egg_directory = findMainDirectoryFile("Eggs");
		size_t eggs_checked = 0;
		if (!egg_directory.empty()) {
			for (const auto& entry : std::filesystem::directory_iterator(egg_directory)) {
				if (!entry.is_regular_file() || to_ascii_lowercase(entry.path().extension().string()) != ".egg")
					continue;
				FileReader egg_reader(entry.path());
				C2EggFile::read(egg_reader);
				++eggs_checked;
			}
		}
		fmt::print("C2 hatchery files checked: {} (container and parent genomes)\n", eggs_checked);
		auto room = world.map->getRoom(16);
		if (!room)
			throw Exception("Missing the stock C2 incubator room");
		const int x = static_cast<int>((room->x_left + room->x_right) / 2);
		const int y = static_cast<int>(room->floorYatX(static_cast<float>(x))) - 150;
		const auto result = engine.executeNetwork(fmt::format("new: crea tokn norn 1\nsetv norn targ\nmvto {} {}\n", x, y));
		if (!result.empty())
			throw Exception(result);
		AgentRef newborn = world.selectedcreature;
		auto* agent = dynamic_cast<CreatureAgent*>(newborn.get());
		auto* creature = agent ? dynamic_cast<c2Creature*>(agent->getCreature()) : nullptr;
		if (!creature)
			throw Exception("NEW: CREA did not produce a C2 Norn");
		const auto moniker = world.history->findMoniker(creature->getGenome());
		if (world.history->getMoniker(moniker).getStatus() != borncreature)
			throw Exception("NEW: CREA did not register the Norn's birth");
		const auto start_age = creature->getAge();
		const auto start_brain_ticks = creature->getBrain()->getTicks();
		for (unsigned int i = 0; i < 1200; ++i) {
			engine.update();
			if (!newborn || !creature->isAlive())
				throw Exception(fmt::format("The newborn Norn died after {} ticks", i + 1));
		}
		if (creature->getAge() <= start_age || creature->getBrain()->getTicks() <= start_brain_ticks)
			throw Exception("The Norn's age or brain did not advance");
		fmt::print("C2 gameplay check passed: 1200 ticks, age {}, brain ticks {}, organs {}\n", creature->getAge(), creature->getBrain()->getTicks(), creature->noOrgans());
		engine.shutdown();
		return 0;
	} catch (const std::exception& error) {
		fmt::print(stderr, "C2 gameplay check failed: {}\n", error.what());
		if (initialized)
			engine.shutdown();
		return 1;
	}
}

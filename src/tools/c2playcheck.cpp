#include "common/Exception.h"
#include "common/Ascii.h"
#include "common/io/FileReader.h"
#include "common/io/FileWriter.h"
#include "common/io/SpanReader.h"
#include "common/io/StringWriter.h"
#include "common/io/VectorWriter.h"
#include "openc2e/Bubble.h"
#include "fileformats/C2EggFile.h"
#include "fileformats/genomeFile.h"
#include "openc2e/Engine.h"
#include "openc2e/Map.h"
#include "openc2e/PathResolver.h"
#include "openc2e/Room.h"
#include "openc2e/World.h"
#include "openc2e/caosScript.h"
#include "openc2e/caosVM.h"
#include "openc2e/creatures/CreatureAgent.h"
#include "openc2e/creatures/oldBrain.h"
#include "openc2e/creatures/oldCreature.h"
#include "openc2e/historyManager.h"
#include "sdlbackend/SDLMixerBackend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fmt/core.h>
#include <memory>
#include <optional>
#include <string>

static int32_t monikerToken(const std::string& moniker) {
	if (moniker.size() != 4)
		throw Exception("A C2 moniker must be four characters long");
	uint32_t token = 0;
	for (unsigned int i = 0; i < 4; ++i)
		token |= static_cast<uint32_t>(static_cast<uint8_t>(moniker[i])) << (i * 8);
	int32_t signed_token;
	std::memcpy(&signed_token, &token, sizeof(token));
	return signed_token;
}

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
		std::array<std::string, 8> args = {argv[0], "--backend", "null", "--audiobackend", "sdlmixer", "--silent", "--data-path", argv[1]};
		std::array<char*, 8> pointers;
		for (size_t i = 0; i < args.size(); ++i)
			pointers[i] = args[i].data();
		if (!engine.parseCommandLine(static_cast<int>(pointers.size()), pointers.data()) || !engine.initialSetup())
			throw Exception("Could not initialize the C2 integration world");
		initialized = true;
		if (engine.gametype != "c2")
			throw Exception("The data directory must contain Creatures 2");
		const auto egg_directory = findMainDirectoryFile("Eggs");
		size_t eggs_checked = 0;
		size_t different_parent_eggs = 0;
		std::optional<C2EggFile> egg_to_hatch;
		if (!egg_directory.empty()) {
			for (const auto& entry : std::filesystem::directory_iterator(egg_directory)) {
				if (!entry.is_regular_file() || to_ascii_lowercase(entry.path().extension().string()) != ".egg")
					continue;
				FileReader egg_reader(entry.path());
				auto egg = C2EggFile::read(egg_reader);
				if (!egg.father_genome.empty() && egg.mother_genome != egg.father_genome)
					++different_parent_eggs;
				if (!egg_to_hatch)
					egg_to_hatch = std::move(egg);
				++eggs_checked;
			}
		}
		fmt::print("C2 hatchery files checked: {} ({} with different parent genomes)\n", eggs_checked, different_parent_eggs);
		if (!egg_to_hatch)
			throw Exception("No installed C2 egg was found");
		const auto check_data_directory = std::filesystem::current_path() / "build" / "c2playcheck-user-data";
		std::filesystem::create_directories(check_data_directory);
		data_directories.emplace_back(check_data_directory);
		SpanReader genome_reader(egg_to_hatch->mother_genome);
		auto genome = std::make_shared<genomeFile>();
		genome_reader >> *genome;
		if (genome->getVersion() != 2)
			throw Exception("The selected C2 egg does not contain a version 2 genome");
		const std::string mother_moniker = "mum0";
		const std::string father_moniker = "dad0";
		std::shared_ptr<genomeFile> father_genome;
		for (const auto& candidate_moniker : {"norn", "gold", "test"}) {
			auto candidate = world.loadGenome(candidate_moniker);
			if (!candidate || candidate->getVersion() != 2)
				continue;
			VectorWriter encoded_candidate;
			encoded_candidate << *candidate;
			if (encoded_candidate.vector() != egg_to_hatch->mother_genome) {
				father_genome = std::move(candidate);
				break;
			}
		}
		if (!father_genome)
			throw Exception("Could not find a second distinct C2 parent genome for the crossing check");
		for (const auto& parent : {std::pair<std::string, std::shared_ptr<genomeFile>>(mother_moniker, genome), {father_moniker, father_genome}}) {
			auto genome_file = createUserGeneticsFile(parent.first + ".gen");
			genome_file << *parent.second;
			genome_file.flush();
		}
		auto room = world.map->getRoom(16);
		if (!room)
			throw Exception("Missing the stock C2 incubator room");
		const unsigned int test_ca = CA_COUNT - 1;
		const auto rate_set = world.carates.find(room->type);
		const bool had_previous_rate = rate_set != world.carates.end() && rate_set->second.find(test_ca) != rate_set->second.end();
		const cainfo previous_rate = had_previous_rate ? rate_set->second.at(test_ca) : cainfo{};
		const float previous_ca = room->ca[test_ca];
		const float previous_catemp = room->catemp[test_ca];
		world.carates[room->type][test_ca] = cainfo{0.0f, 0.5f, 0.0f};
		room->ca[test_ca] = 0.5f;
		room->catemp[test_ca] = 0.0f;
		world.map->tick();
		const float decayed_ca = room->ca[test_ca];
		room->ca[test_ca] = previous_ca;
		room->catemp[test_ca] = previous_catemp;
		if (had_previous_rate)
			world.carates[room->type][test_ca] = previous_rate;
		else {
			world.carates[room->type].erase(test_ca);
			if (world.carates[room->type].empty())
				world.carates.erase(room->type);
		}
		if (decayed_ca < 0.249f || decayed_ca > 0.251f)
			throw Exception(fmt::format("C2 room CA tick produced {}, expected 0.25", decayed_ca));
		const int x = static_cast<int>((room->x_left + room->x_right) / 2);
		const int y = static_cast<int>(room->floorYatX(static_cast<float>(x))) - 150;
		auto result = engine.executeNetwork(fmt::format(
			"new: simp eggs 8 {} 2000 0\npose 3\nsetv cls2 2 5 2\nsetv attr 195\n"
			"new: gene {} {} obv0\nsetv obv1 {}\ntick 2400\nmvto {} {}\nsetv grav 1\n"
			"new: crea obv0 0\nsetv norn targ\nmvto {} {}\n",
			static_cast<unsigned int>(egg_to_hatch->image) * 8,
			monikerToken(mother_moniker),
			monikerToken(father_moniker),
			egg_to_hatch->gender,
			x,
			y,
			x,
			y));
		if (!result.empty())
			throw Exception("Installed two-parent egg and creature creation failed: " + result);
		AgentRef newborn = world.selectedcreature;
		auto* agent = dynamic_cast<CreatureAgent*>(newborn.get());
		auto* creature = agent ? dynamic_cast<c2Creature*>(agent->getCreature()) : nullptr;
		if (!creature)
			throw Exception("NEW: CREA did not produce a C2 Norn");
		const auto moniker = world.history->findMoniker(creature->getGenome());
		if (world.history->getMoniker(moniker).getStatus() != borncreature)
			throw Exception("NEW: CREA did not register the Norn's birth");
		const auto* genus = dynamic_cast<const creatureGenusGene*>(creature->getGenome()->getGene(2, 1, 1));
		if (!genus || genus->mum != mother_moniker || genus->dad != father_moniker)
			throw Exception("The crossed genome did not preserve both parent monikers");
		const auto run_c2_agent_script = [&](const std::string& source) {
			caosScript script("c2", "<C2 gameplay check>");
			script.parse(source);
			caosVM vm(newborn);
			StringWriter output;
			vm.outputstream = &output;
			struct reset_output_stream {
				caosVM& vm;
				~reset_output_stream() { vm.outputstream = nullptr; }
			} reset{vm};
			vm.runEntirely(script.installer);
			return output.string();
		};
		const auto timer_start = run_c2_agent_script("tick 8\noutv tick");
		if (timer_start != "8")
			throw Exception(fmt::format("C2 TICK returned {} after setting an 8-tick timer, expected 8", timer_start));
		for (unsigned int i = 0; i < 3; ++i)
			engine.update();
		const auto timer_remaining = run_c2_agent_script("outv tick");
		if (timer_remaining != "5")
			throw Exception(fmt::format("C2 TICK returned {} after 3 world ticks, expected 5", timer_remaining));
		const auto saved_world_tick = world.worldtickcount;
		const auto check_elapsed_clock = [&](unsigned int ticks, const std::string& expected) {
			world.worldtickcount = ticks;
			const auto actual = run_c2_agent_script(R"(outv hour
outs ":"
outv mins)");
			world.worldtickcount = saved_world_tick;
			if (actual != expected)
				throw Exception(fmt::format("C2 HOUR/MINS returned {} at world tick {}, expected {}", actual, ticks, expected));
		};
		check_elapsed_clock(0, "0:0");
		check_elapsed_clock(20 * 60 * 60 - 1, "0:59");
		check_elapsed_clock(20 * 60 * 60, "1:0");
		const int decision = creature->getDecisionId();
		if (decision >= 0) {
			auto* decision_lobe = creature->getBrain()->getLobeByTissue(6);
			if (!decision_lobe || static_cast<unsigned int>(decision) >= decision_lobe->getNoNeurons())
				throw Exception("C2 creature reported an invalid decision neuron");
			auto* decision_neuron = decision_lobe->getNeuron(static_cast<unsigned int>(decision));
			const unsigned char initial_state = decision_neuron->state;
			(void)run_c2_agent_script("targ ownr impt 7");
			const auto expected_state = static_cast<unsigned char>(std::min(255, static_cast<int>(initial_state) + 7));
			if (decision_neuron->state != expected_state)
				throw Exception("C2 IMPT did not nudge the current decision neuron");
		}
		(void)run_c2_agent_script("targ ownr done");
		if (creature->getDecisionId() != -1)
			throw Exception("C2 DONE did not clear the current decision");
		(void)run_c2_agent_script("targ ownr say$ \"playcheck\"");
		bool found_speech_bubble = false;
		for (const auto& candidate : world.agents) {
			auto* bubble = candidate ? dynamic_cast<Bubble*>(candidate.get()) : nullptr;
			if (bubble && bubble->getText() == "playcheck")
				found_speech_bubble = true;
		}
		if (!found_speech_bubble)
			throw Exception("C2 SAY$ did not create the requested speech bubble");
		const float walk_start_x = newborn->x;
		agent->setDirection(2);
		agent->startWalking();
		for (unsigned int i = 0; i < 80; ++i)
			engine.update();
		agent->stopWalking();
		const float walked_pixels = std::fabs(newborn->x - walk_start_x);
		if (walked_pixels < 1.0f)
			throw Exception(fmt::format("C2 Norn did not move during 80 forced walking ticks (x={}, start={})", newborn->x, walk_start_x));
		const auto start_age = creature->getAge();
		const auto start_brain_ticks = creature->getBrain()->getTicks();
		for (unsigned int i = 0; i < 3600; ++i) {
			engine.update();
			if (!newborn || !creature->isAlive())
				throw Exception(fmt::format("The newborn Norn died after {} ticks", i + 1));
		}
		if (creature->getAge() <= start_age || creature->getBrain()->getTicks() <= start_brain_ticks)
			throw Exception("The Norn's age or brain did not advance");
		unsigned int related_norns = 0;
		for (const auto& candidate : world.agents) {
			if (!candidate)
				continue;
			auto* candidate_agent = dynamic_cast<CreatureAgent*>(candidate.get());
			auto* candidate_creature = candidate_agent ? dynamic_cast<c2Creature*>(candidate_agent->getCreature()) : nullptr;
			if (!candidate_creature || !candidate_creature->isAlive())
				continue;
			const auto* candidate_genus = dynamic_cast<const creatureGenusGene*>(candidate_creature->getGenome()->getGene(2, 1, 1));
			if (candidate_genus && candidate_genus->mum == mother_moniker && candidate_genus->dad == father_moniker)
				++related_norns;
		}
		if (related_norns < 2)
			throw Exception("The placed two-parent C2 egg did not hatch into a second living Norn within 3,600 ticks");
		fmt::print("C2 gameplay check passed: 3600 ticks, {} related living Norns, walked {} pixels, age {}, brain ticks {}, organs {}\n", related_norns, walked_pixels, creature->getAge(), creature->getBrain()->getTicks(), creature->noOrgans());
		engine.shutdown();
		return 0;
	} catch (const std::exception& error) {
		fmt::print(stderr, "C2 gameplay check failed: {}\n", error.what());
		if (initialized)
			engine.shutdown();
		return 1;
	}
}

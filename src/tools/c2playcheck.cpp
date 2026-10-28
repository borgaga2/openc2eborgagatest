#include "common/Exception.h"
#include "AgentHelpers.h"
#include "common/Ascii.h"
#include "common/io/FileReader.h"
#include "common/io/FileWriter.h"
#include "common/io/SpanReader.h"
#include "common/io/StringWriter.h"
#include "common/io/VectorWriter.h"
#include "openc2e/Bubble.h"
#include "openc2e/Camera.h"
#include "fileformats/C2EggFile.h"
#include "fileformats/genomeFile.h"
#include "fileformats/sfc/C2SFCFile.h"
#include "fileformats/sfc/C2CreatureData.h"
#include "fileformats/MFCWriter.h"
#include "fileformats/s16Image.h"
#include "common/creaturesImage.h"
#include "common/backend/Backend.h"
#include "openc2e/Engine.h"
#include "openc2e/Map.h"
#include "openc2e/PointerAgent.h"
#include "openc2e/PathResolver.h"
#include "openc2e/Room.h"
#include "openc2e/SimpleAgent.h"
#include "openc2e/World.h"
#include "openc2e/caosScript.h"
#include "openc2e/caosVM.h"
#include "openc2e/creatures/CreatureAgent.h"
#include "openc2e/creatures/SkeletalCreature.h"
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

class SavedC2CreatureCheck : public c2Creature {
  public:
	using c2Creature::c2Creature;
	void sampleBiochemistry() { tickBiochemistry(); }
};

class SavedBodyRenderCheck : public RenderTarget {
  public:
	struct Draw { const void* pixels; int x, y; };
	std::vector<Draw> draws;
	void renderCreaturesImage(creaturesImage& image, unsigned int frame, int x, int y, RenderOptions options) override {
		if (options.mirror || frame >= image.numframes()) throw Exception("Unexpected saved body rendering");
		draws.push_back({image.data(frame), x, y});
	}
	void renderTexture(const Texture&, Rect2i, Rect2f, RenderOptions) override {}
	void renderLine(float, float, float, float, Color) override {}
	void blitRenderTarget(RenderTarget*, Rect2f) override {}
	int32_t getWidth() const override { return 800; }
	int32_t getHeight() const override { return 600; }
	void setClip(Rect2f) override {}
	void renderClear() override { draws.clear(); }
	void setViewportOffsetTop(int) override {}
	void setViewportOffsetBottom(int) override {}
};

static void checkSavedBody(SkeletalCreature& holder, const sfc::CreatureV2& saved,
	const std::filesystem::path& save_path) {
	std::map<std::string, std::shared_ptr<creaturesImage>> images;
	auto load = [&](const std::string& name) {
		auto& image = images[name];
		if (!image) {
			if (name.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos || name.empty())
				throw Exception("Invalid saved composite sprite name");
			auto path = save_path.parent_path() / "Images" / (name + ".s16");
			if (!std::filesystem::exists(path)) path = save_path.parent_path() / (name + ".s16");
			FileReader input(path);
			image = std::make_shared<creaturesImage>(name);
			image->images = ReadS16File(input);
		}
		return image;
	};
	holder.restoreC2Body(saved, load);
	const auto check_pose = [&](const char* phase) {
	SavedBodyRenderCheck target;
	holder.render(&target, static_cast<int>(holder.x), static_cast<int>(holder.y));
	std::vector<const sfc::BodyPartV2*> parts{saved.body.get()};
	for (auto limb : saved.limbs)
		for (; limb; limb = limb->next) parts.push_back(limb.get());
	if (target.draws.size() != parts.size()) throw Exception("Restored C2 body has the wrong number of rendered parts");
	for (const auto* part : parts) {
		const auto gallery = load(part->gallery->filename);
		const auto frame = part->gallery->absolute_base + part->pose_plus_base;
		const auto found = std::find_if(target.draws.begin(), target.draws.end(), [&](const auto& draw) {
			return draw.pixels == gallery->data(frame) && draw.x == part->x && draw.y == part->y;
		});
		if (found == target.draws.end()) {
			const auto same_frame = std::find_if(target.draws.begin(), target.draws.end(), [&](const auto& draw) {
				return draw.pixels == gallery->data(frame);
			});
			throw Exception(fmt::format("Restored C2 {} body {}: expected frame {} at {},{}; matching frame at {},{}", saved.moniker,
				phase, frame, part->x, part->y, same_frame == target.draws.end() ? -1 : same_frame->x,
				same_frame == target.draws.end() ? -1 : same_frame->y));
		}
		target.draws.erase(found);
	}
	const unsigned int foot = saved.downfoot_right ? 12 : 11;
	if (holder.x + holder.attachmentX(foot, 1) != saved.foot_x ||
		holder.y + holder.attachmentY(foot, 1) != saved.foot_y)
		throw Exception(fmt::format("Restored C2 {} planted foot differs: saved {},{}; left {},{}; right {},{}", saved.moniker,
			saved.foot_x, saved.foot_y, holder.x + holder.attachmentX(11, 1), holder.y + holder.attachmentY(11, 1),
			holder.x + holder.attachmentX(12, 1), holder.y + holder.attachmentY(12, 1)));
	};
	check_pose("on restore");
	holder.setPose(saved.current_pose);
	check_pose("after recalculation");
	fmt::print("C2 saved body check passed: {} ({} sprite frames, positions and planted foot, before and after pose recalculation)\n", saved.moniker, saved.limbs[5] ? 14 : 12);
}

// An opt-in integration check against a user's installed C2 assets. It runs
// the real world, scripts, skeleton and biochemistry with the null renderer.
int main(int argc, char** argv) {
	if (argc != 2 && argc != 4) {
		fmt::print(stderr, "USAGE: {} CREATURES_2_DATA_DIRECTORY [SAVED_WORLD GENETICS_DIRECTORY]\n", argv[0]);
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
		const auto world_archive_path = findMainDirectoryFile("World.sfc");
		if (world_archive_path.empty())
			throw Exception("Could not locate the installed C2 World.sfc archive");
		FileReader world_archive_file(world_archive_path);
		const auto world_archive_bytes = world_archive_file.read_to_end();
		SpanReader world_archive_input(world_archive_bytes);
		auto world_archive = sfc::read_c2_sfc_file(world_archive_input);
		VectorWriter world_archive_output;
		sfc::write_c2_sfc_file(world_archive_output, world_archive);
		if (world_archive_output.vector() != world_archive_bytes)
			throw Exception("Installed C2 World.sfc did not survive an exact archive round trip");
		fmt::print("C2 world archive check passed: {} room slots, {} objects, {} sceneries, {} macros, {} unknown tail bytes\n",
			world_archive.map->rooms.size(), world_archive.objects.size(), world_archive.sceneries.size(),
			world_archive.script_state.macros.size(), world_archive.trailing_data.size());
		if (argc == 4) {
			auto saved_world = sfc::read_c2_sfc_file(std::filesystem::path(argv[2]));
			size_t checked = 0, neurons = 0, connections = 0, organs_checked = 0;
			size_t receptors_checked = 0, emitters_checked = 0, reactions_checked = 0;
			size_t biochemistries_checked = 0;
			size_t instincts_checked = 0;
			const auto encode_instincts = [](const sfc::CreatureV2& creature) {
				VectorWriter output;
				MFCWriter writer(output);
				writer.register_class<sfc::CInstinctV1>("CInstinct", 1);
				for (const auto& instinct : creature.instincts)
					writer(instinct);
				return output.vector();
			};
			const auto same_physiology = [](const sfc::CreatureV2& a, const sfc::CreatureV2& b) {
				return a.physiological_state == b.physiological_state && a.sex == b.sex &&
					a.age == b.age && a.age_ticks == b.age_ticks && a.biotick == b.biotick &&
					a.life_state == b.life_state && a.asleep == b.asleep;
			};
			const auto encode_biochemistry = [](const sfc::CBiochemistryV2& biology) {
				VectorWriter output;
				MFCWriter writer(output);
				sfc::register_c2_brain_class(writer);
				sfc::register_c2_biochemistry_classes(writer);
				auto record = biology;
				record.owner = nullptr; // compare this component without serializing its world owner
				writer(&record);
				return output.vector();
			};
			const auto encode_organ = [](const sfc::COrganV2& organ) {
				VectorWriter output;
				MFCWriter writer(output);
				sfc::register_c2_organ_classes(writer);
				auto organ_record = organ; // include only the organ base of a saved brain
				writer(&organ_record);
				return output.vector();
			};
			const auto encode_brain = [](const sfc::CBrainV2& brain) {
				VectorWriter output;
				MFCWriter writer(output);
				sfc::register_c2_brain_class(writer);
				writer(std::make_shared<sfc::CBrainV2>(brain));
				return output.vector();
			};
			for (const auto& object : saved_world.objects) {
				auto saved = std::dynamic_pointer_cast<sfc::CreatureV2>(object);
				if (!saved)
					continue;
				if (saved->moniker.size() != 4 || saved->moniker.find_first_not_of(
					"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos)
					throw Exception("Saved C2 creature has an invalid moniker");
				FileReader genetics(std::filesystem::path(argv[3]) / (saved->moniker + ".gen"));
				auto saved_genome = std::make_shared<genomeFile>();
				genetics >> *saved_genome;
				if (saved_genome->getVersion() != 2 || !saved->brain)
					throw Exception("Saved creature is missing its C2 genome or brain");
				SkeletalCreature holder(4);
				checkSavedBody(holder, *saved, std::filesystem::path(argv[2]));
				SavedC2CreatureCheck restored(saved_genome, saved->sex == 2, 0, &holder);
				auto& brain = *restored.getBrain();
				brain.restoreC2State(*saved->brain);
				restored.restoreC2Physiology(*saved);
				sfc::CreatureV2 captured_physiology;
				restored.updateC2Physiology(captured_physiology);
				if (!same_physiology(captured_physiology, *saved))
					throw Exception("Restored C2 physiology did not preserve its saved signals and life state");
				auto captured = *saved->brain;
				brain.updateC2State(captured);
				if (encode_brain(captured) != encode_brain(*saved->brain))
					throw Exception("Restored C2 brain did not preserve every saved state byte");
				for (unsigned int tick = 0; tick < 8; ++tick)
					brain.tick();
				brain.updateC2State(captured);
				brain.restoreC2State(captured);
				brain.tick();
				if (!saved->biochemistry)
					throw Exception("Saved creature has no C2 chemistry");
				// Restore each saved network independently, then exercise it against
				// the saved chemical concentrations. Full world loading is separate.
				const auto check_organ = [&](const sfc::COrganV2& record) {
					c2Organ organ(&restored, record);
					auto captured_organ = record;
					captured_organ.receptor_groups.clear();
					captured_organ.emitters.clear();
					captured_organ.reactions.clear();
					organ.updateC2State(captured_organ);
					if (encode_organ(captured_organ) != encode_organ(record))
						throw Exception("Restored C2 organ did not preserve its saved state and network");
					for (unsigned int i = 1; i < 256; ++i) {
						restored.subChemical(i, 255);
						restored.addChemical(i, saved->biochemistry->chemicals[i].concentration);
					}
					for (unsigned int i = 0; i < 8; ++i)
						organ.tick();
					organ.updateC2State(captured_organ);
					organ.restoreC2State(captured_organ);
					organ.tick();
					receptors_checked += organ.getReceptorCount();
					emitters_checked += organ.getEmitterCount();
					reactions_checked += organ.getReactionCount();
					++organs_checked;
				};
				check_organ(*saved->brain);
				for (const auto& organ : saved->biochemistry->organs)
					if (organ)
						check_organ(*organ);
				// The restored brain must be in place before binding the whole body.
				brain.restoreC2State(*saved->brain);
				restored.restoreC2Physiology(*saved);
				restored.restoreC2Biochemistry(*saved->biochemistry, saved->biotick);
				sfc::CBiochemistryV2 captured_biology;
				restored.updateC2Biochemistry(captured_biology);
				if (encode_biochemistry(captured_biology) != encode_biochemistry(*saved->biochemistry) ||
					restored.getBiochemistryTicks() != saved->biotick)
					throw Exception("Restored C2 biochemistry did not preserve its full saved graph and timing");
				for (unsigned int i = 0; i < 32; ++i)
					restored.sampleBiochemistry();
				brain.updateC2State(captured);
				restored.updateC2Biochemistry(captured_biology);
				restored.updateC2Physiology(captured_physiology);
				brain.restoreC2State(captured);
				restored.restoreC2Physiology(captured_physiology);
				restored.restoreC2Biochemistry(captured_biology, captured_physiology.biotick);
				sfc::CreatureV2 resumed_physiology;
				restored.updateC2Physiology(resumed_physiology);
				if (!same_physiology(resumed_physiology, captured_physiology))
					throw Exception("Resumed C2 physiology lost changes made by its restored biochemistry");
				restored.sampleBiochemistry();
				++biochemistries_checked;
				restored.restoreC2Instincts(*saved);
				sfc::CreatureV2 captured_instincts;
				restored.updateC2Instincts(captured_instincts);
				if (encode_instincts(captured_instincts) != encode_instincts(*saved) ||
					captured_instincts.dreaming != saved->dreaming ||
					restored.getNoUnprocessedInstincts() != saved->instincts.size())
					throw Exception("Restored C2 instincts lost their queue, aliases, phase or dreaming limit");
				instincts_checked += saved->instincts.size();
				++checked;
				neurons += saved->brain->neurons.size();
				for (const auto& neuron : saved->brain->neurons)
					connections += neuron.dendrites0.size() + neuron.dendrites1.size();
			}
			if (!checked)
				throw Exception("The optional saved C2 world contained no creatures");
			fmt::print("C2 saved brain check passed: {} creatures, {} neurons, {} learned connections\n", checked, neurons, connections);
			fmt::print("C2 saved organ state check passed: {} records, {} receptors, {} emitters, {} reactions\n",
				organs_checked, receptors_checked, emitters_checked, reactions_checked);
			fmt::print("C2 saved biochemistry graph check passed: {} creatures\n", biochemistries_checked);
			fmt::print("C2 saved physiology check passed: {} creatures\n", checked);
			fmt::print("C2 saved instinct queue check passed: {} instincts across {} creatures\n", instincts_checked, checked);
		}
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
		const auto run_c2_agent_script_as = [&](AgentRef owner, AgentRef it, const std::string& source) {
			caosScript script("c2", "<C2 gameplay check>");
			script.parse(source);
			caosVM vm(owner);
			vm.targ = owner;
			vm._it_ = it;
			StringWriter output;
			vm.outputstream = &output;
			struct reset_output_stream {
				caosVM& vm;
				~reset_output_stream() { vm.outputstream = nullptr; }
			} reset{vm};
			vm.runEntirely(script.installer);
			return output.string();
		};
		const auto run_c2_agent_script = [&](const std::string& source) {
			return run_c2_agent_script_as(newborn, AgentRef(), source);
		};
		(void)run_c2_agent_script("gene load targ 3 \"norn\"");
		const auto loaded_genome = newborn->genome_slots.find(3);
		if (loaded_genome == newborn->genome_slots.end() || !loaded_genome->second || loaded_genome->second->getVersion() != 2)
			throw Exception("C2 GENE LOAD did not place the selected genome in its requested slot");
		const size_t instincts_before_doin = creature->getNoUnprocessedInstincts();
		if (instincts_before_doin < 2)
			throw Exception("Newborn C2 Norn did not have enough queued instincts for DOIN check");
		const auto reported_instincts = run_c2_agent_script("outv ins#");
		if (reported_instincts != std::to_string(instincts_before_doin))
			throw Exception(fmt::format("C2 INS# returned {}, expected {}", reported_instincts, instincts_before_doin));
		(void)run_c2_agent_script("doin 2");
		if (creature->getNoUnprocessedInstincts() != instincts_before_doin - 2)
			throw Exception("C2 DOIN did not process exactly the requested number of instincts");
		const size_t instincts_before_dream = creature->getNoUnprocessedInstincts();
		(void)run_c2_agent_script("drea 1");
		if (!creature->isDreaming() || !creature->isAsleep())
			throw Exception("C2 DREA did not put the Norn to sleep and start dreaming");
		for (size_t tick = 0; tick < instincts_before_dream; ++tick)
			creature->tick();
		if (creature->isDreaming() || !creature->isAsleep() || creature->getNoUnprocessedInstincts() != 0)
			throw Exception("C2 DREA did not process the baby's full instinct queue and end dreaming");
		creature->setAsleep(false); // Restore the newborn for the remaining interaction checks.
		(void)run_c2_agent_script("mesg writ _it_ 0");
		const float saved_obsv_x = newborn->x;
		const float saved_obsv_y = newborn->y;
		const int saved_obsv_size = newborn->size;
		bool checked_obsv_door = false;
		for (unsigned int room_id = 0; room_id < world.map->getRoomCount() && !checked_obsv_door; ++room_id) {
			auto source_room = world.map->getRoom(room_id);
			if (!source_room)
				continue;
			for (const auto& [destination_room, door] : source_room->getDoors()) {
				(void)door;
				if (source_room->x_right != destination_room->x_left || source_room->x_right <= newborn->getWidth() + 1)
					continue;
				const unsigned int first_y = std::max({source_room->y_left_ceiling, source_room->y_right_ceiling, destination_room->y_left_ceiling, destination_room->y_right_ceiling}) + newborn->getHeight();
				const unsigned int last_y = std::min({source_room->y_left_floor, source_room->y_right_floor, destination_room->y_left_floor, destination_room->y_right_floor}) - newborn->getHeight();
				if (first_y >= last_y)
					continue;
				for (unsigned int boundary_y = first_y; boundary_y <= last_y; ++boundary_y) {
					if (!source_room->containsPoint(source_room->x_right - 1, boundary_y) || !destination_room->containsPoint(destination_room->x_left + 1, boundary_y))
						continue;
					const int saved_door_perm = world.map->getDoorPerm(source_room, destination_room);
					world.map->setDoorPerm(source_room, destination_room, 37);
					newborn->size = 38;
					newborn->moveTo(static_cast<float>(source_room->x_right - newborn->getWidth() - 1), static_cast<float>(boundary_y - newborn->getHeight() / 2), true);
					std::string observed_door_perm;
					try {
						observed_door_perm = run_c2_agent_script("outv obsv 1");
					} catch (...) {
						world.map->setDoorPerm(source_room, destination_room, saved_door_perm);
						newborn->size = saved_obsv_size;
						newborn->moveTo(saved_obsv_x, saved_obsv_y, true);
						throw;
					}
					world.map->setDoorPerm(source_room, destination_room, saved_door_perm);
					newborn->size = saved_obsv_size;
					newborn->moveTo(saved_obsv_x, saved_obsv_y, true);
					if (observed_door_perm != "37")
						throw Exception(fmt::format("C2 OBSV returned {} for a blocked door with permeability 37", observed_door_perm));
					checked_obsv_door = true;
					break;
				}
				if (checked_obsv_door)
					break;
			}
		}
		if (!checked_obsv_door)
			throw Exception("Could not find a horizontal C2 room door to check OBSV against");
		const auto timer_start = run_c2_agent_script("tick 8\noutv tick");
		if (timer_start != "8")
			throw Exception(fmt::format("C2 TICK returned {} after setting an 8-tick timer, expected 8", timer_start));
		for (unsigned int i = 0; i < 3; ++i)
			engine.update();
		const auto timer_remaining = run_c2_agent_script("outv tick");
		if (timer_remaining != "5")
			throw Exception(fmt::format("C2 TICK returned {} after 3 world ticks, expected 5", timer_remaining));
		auto* enum_first = new SimpleAgent(254, 254, 65534, 0, "a00a", 0, 1);
		enum_first->finishInit();
		AgentRef enum_first_ref(enum_first);
		auto* enum_second = new SimpleAgent(254, 254, 65534, 0, "a00a", 0, 1);
		enum_second->finishInit();
		AgentRef enum_second_ref(enum_second);
		caosScript enum_script("c2", "<C2 stale enumeration check>");
		enum_script.parse("enum 254 254 65534\nkill targ\nnext");
		caosVM enum_vm(newborn);
		if (!enum_vm.fireScript(enum_script.installer, false))
			throw Exception("Could not start the C2 stale enumeration check script");
		enum_vm.tick(); // Run ENUM, then pause at its normal script yield.
		if (enum_vm.stopped())
			throw Exception("The C2 stale enumeration check did not pause after collecting agents");
		auto enum_first_hold = enum_first_ref.lock();
		if (!enum_first_hold)
			throw Exception("The C2 stale enumeration test agent disappeared before it was killed");
		enum_first_hold->kill(); // Expire a weak reference already stored in the ENUM stack.
		enum_first_hold.reset();
		for (unsigned int i = 0; i < 20 && !enum_vm.stopped(); ++i) {
			enum_vm.timeslice = 1;
			enum_vm.tick();
		}
		if (!enum_vm.stopped())
			throw Exception("The C2 stale enumeration check did not finish");
		if (enum_first_ref.get() || enum_second_ref.get())
			throw Exception("The C2 ENUM ... NEXT loop left a matching test agent alive");
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
		auto* trig_lobe = creature->getBrain()->getLobeByTissue(1);
		if (!trig_lobe || !trig_lobe->getNoNeurons())
			throw Exception("C2 Norn is missing the drive lobe needed for the TRIG check");
		constexpr unsigned int trig_cell = 0;
		constexpr unsigned char trig_amount = 173;
		(void)run_c2_agent_script(fmt::format("targ ownr trig 1 {} {}", trig_cell, static_cast<unsigned int>(trig_amount)));
		const oldNeuron* triggered_neuron = trig_lobe->getNeuron(trig_cell);
		if (triggered_neuron->state != trig_amount || triggered_neuron->output != trig_amount)
			throw Exception("C2 TRIG did not fire the requested neuron at the requested strength");
		(void)run_c2_agent_script("targ ownr trig 255 0 173"); // Invalid lobe indices are ignored safely.
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
		unsigned int most_pressing_drive = 0;
		for (unsigned int drive = 1; drive < 17; ++drive) {
			if (creature->getDrive(drive) > creature->getDrive(most_pressing_drive))
				most_pressing_drive = drive;
		}
		const size_t expected_need_word = 56 + most_pressing_drive;
		if (expected_need_word >= engine.wordlist.size())
			throw Exception("C2 executable word list is missing the built-in need words");
		size_t matching_need_bubbles_before = 0;
		for (const auto& candidate : world.agents) {
			auto* bubble = candidate ? dynamic_cast<Bubble*>(candidate.get()) : nullptr;
			if (bubble && bubble->getText() == engine.wordlist[expected_need_word])
				++matching_need_bubbles_before;
		}
		(void)run_c2_agent_script("targ ownr sayn");
		size_t matching_need_bubbles_after = 0;
		for (const auto& candidate : world.agents) {
			auto* bubble = candidate ? dynamic_cast<Bubble*>(candidate.get()) : nullptr;
			if (bubble && bubble->getText() == engine.wordlist[expected_need_word])
				++matching_need_bubbles_after;
		}
		if (matching_need_bubbles_after <= matching_need_bubbles_before)
			throw Exception("C2 SAYN did not speak the word matching its strongest drive");
		if (engine.wordlist.size() <= 58)
			throw Exception("C2 executable word list is missing word 58");
		const std::string signalled_word = engine.wordlist[58];
		size_t signalled_word_bubbles_before = 0;
		for (const auto& candidate : world.agents) {
			auto* bubble = candidate ? dynamic_cast<Bubble*>(candidate.get()) : nullptr;
			if (bubble && bubble->getText() == signalled_word)
				++signalled_word_bubbles_before;
		}
		(void)run_c2_agent_script("targ ownr say# 58");
		size_t signalled_word_bubbles_after = 0;
		for (const auto& candidate : world.agents) {
			auto* bubble = candidate ? dynamic_cast<Bubble*>(candidate.get()) : nullptr;
			if (bubble && bubble->getText() == signalled_word)
				++signalled_word_bubbles_after;
		}
		if (signalled_word_bubbles_after <= signalled_word_bubbles_before)
			throw Exception("C2 SAY# did not speak its built-in word");
		auto touch_target = std::find_if(world.agents.begin(), world.agents.end(), [&](const std::shared_ptr<Agent>& candidate) {
			return candidate && candidate.get() != newborn.get() && !dynamic_cast<CreatureAgent*>(candidate.get()) &&
				!dynamic_cast<Bubble*>(candidate.get()) && candidate->findScript(1);
		});
		if (touch_target == world.agents.end())
			throw Exception("Could not find a C2 object to test creature touching");
		AgentRef interaction_target = *touch_target;
		const float saved_target_x = interaction_target->x;
		const float saved_target_y = interaction_target->y;
		const int saved_target_actv = interaction_target->actv;
		const unsigned int saved_touch_behavior = (interaction_target->cr_can_push ? 1u : 0u) |
			(interaction_target->cr_can_pull ? 2u : 0u) | (interaction_target->cr_can_stop ? 4u : 0u) |
			(interaction_target->cr_can_hit ? 8u : 0u) | (interaction_target->cr_can_eat ? 16u : 0u) |
			(interaction_target->cr_can_pickup ? 32u : 0u);
		interaction_target->moveTo(newborn->x + newborn->getWidth() + 8, newborn->y);
		if (agentsTouching(newborn.get(), interaction_target.get()))
			throw Exception("C2 TOUC test target starts inside the creature's reach");
		interaction_target->actv = 2;
		interaction_target->setC2TouchBehavior(1);
		caosScript touch_script("c2", "<C2 touch action check>");
		touch_script.parse("targ ownr aim: 0\ntarg ownr touc");
		caosVM touch_vm(newborn);
		if (!touch_vm.fireScript(touch_script.installer, false))
			throw Exception("Could not start the C2 touch action check script");
		touch_vm._it_ = interaction_target;
		for (unsigned int tick = 0; tick < 1200 && !(touch_vm.stopped() && interaction_target->actv != 2); ++tick) {
			if (!touch_vm.timeslice)
				touch_vm.timeslice = 1;
			touch_vm.tick();
			engine.update();
		}
		const int target_actv_after_touch = interaction_target->actv;
		const bool target_received_touch_action = touch_vm.stopped() && target_actv_after_touch != 2;
		interaction_target->moveTo(saved_target_x, saved_target_y);
		interaction_target->actv = saved_target_actv;
		interaction_target->setC2TouchBehavior(saved_touch_behavior);
		if (!target_received_touch_action)
			throw Exception(fmt::format(
				"C2 TOUC did not finish its approach and activate the target (actv {}, stopped {}, blocked {}, touching {}, aim {}, creature x/w {},{}, target x/w {},{})",
				target_actv_after_touch, touch_vm.stopped(), touch_vm.blocking != nullptr,
				agentsTouching(newborn.get(), interaction_target.get()), agent->getAimAction(), newborn->x,
				newborn->getWidth(), interaction_target->x, interaction_target->getWidth()));
		std::shared_ptr<Agent> pointer_target;
		int pointer_script = 0;
		for (const auto& candidate : world.agents) {
			if (!candidate || candidate.get() == newborn.get() || candidate.get() == interaction_target.get() ||
				dynamic_cast<CreatureAgent*>(candidate.get()) || dynamic_cast<Bubble*>(candidate.get()))
				continue;
			for (int event : {1, 2}) {
				if (candidate->findScript(event) && candidate->getLastScript() != event) {
					pointer_target = candidate;
					pointer_script = event;
					break;
				}
			}
			if (pointer_target)
				break;
		}
		if (!pointer_target)
			throw Exception("Could not find a C2 object with an unambiguous activation script for pointer testing");
		const unsigned int saved_target_attributes = pointer_target->attr;
		const std::array<int, 3> saved_click_actions = {
			pointer_target->clac[0], pointer_target->clac[1], pointer_target->clac[2]};
		const int saved_click_target_actv = pointer_target->actv;
		PointerAgent* hand = world.hand();
		const float saved_hand_x = hand->x;
		const float saved_hand_y = hand->y;
		pointer_target->attr |= 4;
		pointer_target->clac[2] = pointer_script - 1; // C2 Activate 1/2 map to script 1/2.
		pointer_target->actv = 2;
		BackendEvent move_hand{};
		move_hand.type = eventmousemove;
		move_hand.x = pointer_target->x + (pointer_target->getWidth() / 2.0f) - engine.camera->getX();
		move_hand.y = pointer_target->y + (pointer_target->getHeight() / 2.0f) - engine.camera->getY();
		hand->handleEvent(move_hand);
		BackendEvent click_target{};
		click_target.type = eventmousebuttondown;
		click_target.button = buttonleft;
		hand->handleEvent(click_target);
		engine.update();
		const bool pointer_activated_target = pointer_target->getLastScript() == pointer_script;
		pointer_target->attr = saved_target_attributes;
		pointer_target->clac[0] = saved_click_actions[0];
		pointer_target->clac[1] = saved_click_actions[1];
		pointer_target->clac[2] = saved_click_actions[2];
		pointer_target->actv = saved_click_target_actv;
		hand->moveTo(saved_hand_x, saved_hand_y, true);
		if (!pointer_activated_target)
			throw Exception("A C2 hand click did not activate an object under the pointer");
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
		const auto clone_genome = [](const std::shared_ptr<genomeFile>& source) {
			VectorWriter encoded;
			encoded << *source;
			SpanReader reader(encoded.vector());
			auto clone = std::make_shared<genomeFile>();
			reader >> *clone;
			if (reader.has_data_left())
				throw Exception("The C2 MATE check could not clone a parent genome");
			return clone;
		};
		const auto create_mate_test_creature = [&](const std::shared_ptr<genomeFile>& source, bool is_female) {
			auto parent_copy = clone_genome(source);
			auto* skeletal = new SkeletalCreature(4);
			try {
				auto* c2 = new c2Creature(parent_copy, is_female, 0, skeletal);
				skeletal->setCreature(c2);
			} catch (...) {
				delete skeletal;
				throw;
			}
			skeletal->finishInit();
			const std::string moniker = world.history->newMoniker(parent_copy);
			world.history->getMoniker(moniker).moveToCreature(skeletal);
			skeletal->getCreature()->born();
			return AgentRef(skeletal);
		};
		auto mate_male_ref = create_mate_test_creature(father_genome, false);
		auto mate_female_ref = create_mate_test_creature(genome, true);
		auto* mate_male = dynamic_cast<c2Creature*>(dynamic_cast<CreatureAgent*>(mate_male_ref.get())->getCreature());
		auto* mate_female = dynamic_cast<c2Creature*>(dynamic_cast<CreatureAgent*>(mate_female_ref.get())->getCreature());
		auto* female_sensory = mate_female->getBrain()->getLobeByTissue(5);
		if (!female_sensory || !female_sensory->getNoNeurons())
			throw Exception("C2 MATE check could not find a sensory neuron for speech routing checks");
		auto speech_stimulus = std::make_unique<creatureStimulusGene>(2);
		speech_stimulus->header.switchontime = mate_female->getStage();
		speech_stimulus->stim = 11;
		speech_stimulus->significance = 1;
		speech_stimulus->sensoryneuron = 0;
		speech_stimulus->intensity = 64;
		speech_stimulus->modulate = false;
		speech_stimulus->addoffset = false;
		speech_stimulus->whenasleep = false;
		for (unsigned int i = 0; i < 4; ++i) {
			speech_stimulus->silent[i] = false;
			speech_stimulus->drives[i] = 255;
			speech_stimulus->amounts[i] = 124;
		}
		mate_female->getGenome()->genes.push_back(std::move(speech_stimulus));
		const float saved_speaker_x = mate_male_ref->x;
		const float saved_speaker_y = mate_male_ref->y;
		const float saved_listener_x = mate_female_ref->x;
		const float saved_listener_y = mate_female_ref->y;
		const float saved_listener_range = mate_female_ref->range;
		mate_male_ref->moveTo(newborn->x, newborn->y, true);
		mate_female_ref->moveTo(newborn->x + 1.0f, newborn->y, true);
		mate_female_ref->range = 256.0f;
		const std::array<std::array<const char*, 2>, 4> order_speeches = {{
			{{"targ ownr ordr shou \"order-shou\"", "order-shou"}},
			{{"targ ownr ordr sign \"order-sign\"", "order-sign"}},
			{{"targ ownr ordr tact \"order-tact\"", "order-tact"}},
			{{"targ ownr ordr writ _it_ \"order-writ\"", "order-writ"}},
		}};
		for (const auto& [script_source, speech] : order_speeches) {
			female_sensory->getNeuron(0)->state = 0;
			(void)run_c2_agent_script_as(mate_male_ref, mate_female_ref, script_source);
			if (female_sensory->getNeuron(0)->state != 64)
				throw Exception(fmt::format("C2 ORDR {} did not notify the intended listener", speech));
			const bool found_order_bubble = std::any_of(world.agents.begin(), world.agents.end(), [speech](const std::shared_ptr<Agent>& candidate) {
				auto* bubble = candidate ? dynamic_cast<Bubble*>(candidate.get()) : nullptr;
				return bubble && bubble->getText() == speech;
			});
			if (!found_order_bubble)
				throw Exception(fmt::format("C2 ORDR did not create the requested {} speech bubble", speech));
		}
		mate_male_ref->moveTo(saved_speaker_x, saved_speaker_y, true);
		mate_female_ref->moveTo(saved_listener_x, saved_listener_y, true);
		mate_female_ref->range = saved_listener_range;
		auto* male_fertile = mate_male->getLocusPointer(true, 1, 2, 0);
		auto* female_fertile = mate_female->getLocusPointer(true, 1, 2, 0);
		auto* female_receptive = mate_female->getLocusPointer(true, 1, 2, 1);
		if (!male_fertile || !female_fertile || !female_receptive)
			throw Exception("The selected C2 genomes have no reproductive loci for the MATE check");
		*male_fertile = 255;
		*female_fertile = 255;
		*female_receptive = 255;
		caosScript mate_script("c2", "<C2 MATE check>");
		mate_script.parse("mate");
		caosVM mate_vm(mate_male_ref);
		mate_vm.targ = mate_male_ref;
		mate_vm._it_ = mate_female_ref;
		mate_vm.runEntirely(mate_script.installer);
		auto child_genome = mate_female_ref->getSlot(1);
		if (!child_genome || child_genome->getVersion() != 2)
			throw Exception("C2 MATE did not put the conceived genome in the mother's gene slot 1");
		const auto* child_genus = dynamic_cast<const creatureGenusGene*>(child_genome->getGene(2, 1, 1));
		if (!child_genus || child_genus->mum != world.history->findMoniker(mate_female->getGenome()) || child_genus->dad != world.history->findMoniker(mate_male->getGenome()))
			throw Exception("C2 MATE did not preserve both parent monikers in the offspring genome");
		const std::string child_moniker = world.history->findMoniker(child_genome);
		if (mate_female_ref->babymoniker != monikerToken(child_moniker))
			throw Exception("C2 MATE did not set the mother's BABY moniker");
		const auto& mother_events = world.history->getMoniker(world.history->findMoniker(mate_female->getGenome())).events;
		const auto& father_events = world.history->getMoniker(world.history->findMoniker(mate_male->getGenome())).events;
		const auto has_parent_event = [](const std::vector<historyevent>& events, unsigned int eventno, const std::string& child, const std::string& other_parent) {
			return std::any_of(events.begin(), events.end(), [&](const historyevent& event) {
				return event.eventno == eventno && event.monikers[0] == child && event.monikers[1] == other_parent;
			});
		};
		if (!has_parent_event(mother_events, 8, child_moniker, world.history->findMoniker(mate_male->getGenome())))
			throw Exception("C2 MATE did not record the mother's pregnancy event");
		if (!has_parent_event(father_events, 9, child_moniker, world.history->findMoniker(mate_female->getGenome())))
			throw Exception("C2 MATE did not record the father's impregnation event");
		if (*mate_female->getLocusPointer(false, 1, 2, 1) == 0)
			throw Exception("C2 MATE did not set the mother's pregnancy locus");
		if (dynamic_cast<SkeletalCreature*>(mate_female_ref.get())->getPregnancy() != 1)
			throw Exception("C2 MATE did not set the mother's visible pregnancy state");
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


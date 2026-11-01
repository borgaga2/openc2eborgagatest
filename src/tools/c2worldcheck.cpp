#include "openc2e/C2WorldLoader.h"
#include "openc2e/caosVM.h"
#include "openc2e/caosValue.h"
#include "openc2e/Engine.h"
#include "openc2e/World.h"
#include "openc2e/Map.h"
#include "openc2e/Camera.h"
#include "openc2e/SpritePart.h"
#include "openc2e/Bubble.h"
#include "openc2e/creatures/SkeletalCreature.h"
#include "openc2e/creatures/oldCreature.h"
#include "openc2e/creatures/oldBrain.h"
#include "fileformats/sfc/C2SFCFile.h"
#include "fileformats/sfc/C2CreatureData.h"
#include "fileformats/s16Image.h"
#include "common/creaturesImage.h"
#include "common/backend/Backend.h"
#include "common/io/FileReader.h"
#include "common/Exception.h"
#include "sdlbackend/SDLMixerBackend.h"
#include <filesystem>
#include <fmt/core.h>

int main(int argc, char** argv) {
	if (argc != 5 && argc != 6) {
		fmt::print(stderr, "USAGE: {} C2_INSTALL SAVED_WORLD GENETICS_DIRECTORY ISOLATED_USER_DIRECTORY [RESUME_TICKS]\n", argv[0]);
		return 1;
	}
	bool engine_ready = false;
	try {
		unsigned int resume_ticks = 0;
		if (argc == 6) {
			size_t end = 0;
			const auto count = std::stoul(argv[5], &end);
			if (end != std::string(argv[5]).size() || count > 10000)
				throw Exception("Resume tick count must be between 0 and 10000");
			resume_ticks = count;
		}
		const auto saved_path = std::filesystem::absolute(argv[2]);
		const auto saved = sfc::read_c2_sfc_file(saved_path);
		C2WorldAssets assets;
		auto safe_name = [](const std::string& name) {
			if (name.empty() || name.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos)
				throw Exception("Invalid saved asset name");
		};
		assets.genome = [&](const std::string& name) {
			safe_name(name);
			FileReader input(std::filesystem::path(argv[3]) / (name + ".gen"));
			auto genome = std::make_shared<genomeFile>(); input >> *genome; return genome;
		};
		assets.composite = [&](const std::string& name) {
			safe_name(name);
			auto path = saved_path.parent_path() / "Images" / (name + ".s16");
			if (!std::filesystem::exists(path)) path = saved_path.parent_path() / (name + ".s16");
			FileReader input(path);
			auto image = std::make_shared<creaturesImage>(name); image->images = ReadS16File(input); return image;
		};
		engine.addPossibleAudioBackend("sdlmixer", SDLMixerBackend::get_instance());
		std::vector<std::string> args{argv[0], "--backend", "null", "--audiobackend", "sdlmixer", "--silent", "--data-path", argv[1], "--user-data-path", argv[4]};
		std::vector<char*> pointers; for (auto& arg : args) pointers.push_back(arg.data());
		std::map<const sfc::ObjectV2*, AgentRef> restored;
		if (!engine.parseCommandLine(pointers.size(), pointers.data()) ||
			!engine.initialSetup([&] { engine_ready = true; restored = loadC2WorldSnapshot(saved, assets); }))
			throw Exception("Could not reconstruct saved C2 world");
		if (!world.paused) throw Exception("Snapshot must stay paused until saved scripts are restored");
		size_t creatures = 0, links = 0, parts = 0, bubbles = 0;
		auto check_entity = [&](Agent* agent, unsigned int id, const sfc::EntityV2& entity) {
			auto* part = dynamic_cast<SpritePart*>(agent->part(id));
			if (!part || int64_t(part->getCurrentSprite()) != int64_t(entity.gallery->absolute_base) + entity.pose_plus_base ||
				agent->x + part->x != entity.x || agent->y + part->y != entity.y || part->getZOrder() != static_cast<unsigned int>(entity.plane))
				throw Exception(fmt::format("Saved entity mismatch for {},{},{}, part {}: expected {},{} plane {}; actual {},{} plane {}",
					agent->family, agent->genus, agent->species, id, entity.x, entity.y, entity.plane,
					agent->x + (part ? part->x : 0), agent->y + (part ? part->y : 0), part ? part->getZOrder() : 0));
			if (entity.has_animation) {
				std::vector<uint16_t> animation;
				for (auto c : entity.animation) {
					if (!c) break;
					animation.push_back(c == 'R' ? 255 : c - '0');
				}
				if (part->animation != animation || part->getFrameNo() != entity.animation_frame)
					throw Exception("Lost saved C2 sprite animation or next-frame cursor");
			}
			++parts;
		};
		for (const auto& [s, a] : restored) {
			if (a->getAttributes() != s->attributes || a->actv != s->active ||
				a->size != s->size || a->thrt != s->threat || a->range != s->range ||
				a->accg != s->acceleration || a->rest != static_cast<int32_t>(s->restitution) || a->aero != static_cast<int32_t>(s->aerodynamic_drag) ||
				a->velx != s->velocity_x || a->vely != s->velocity_y ||
				a->falling != (s->gravity_data != 0xffffffff) || a->paused != ((s->flags & 2) != 0))
				throw Exception(fmt::format("Lost saved object state for {}", s->unid));
			for (unsigned int i = 0; i < s->variables.size(); ++i) {
				// The legacy bridge converts several known UNID variables to
				// agent values (bees, tomatoes, nuts, Pitz and the flask).
				const bool matches = a->var[i].hasAgent()
					? a->var[i].getAgent() == world.lookupUNID(s->variables[i])
					: a->var[i].hasInt() && a->var[i].getInt() == static_cast<int32_t>(s->variables[i]);
				if (!matches)
					throw Exception(fmt::format("Lost saved object variable {} for {}", i, s->unid));
			}
			if (!a || world.lookupUNID(s->unid).get() != a.get()) throw Exception("Lost saved object identity");
			if (a->timerrate != s->timer_interval ||
				(a->timerrate && a->getTicksUntilTimer() != std::max(1u, s->timer_state)))
				throw Exception("Lost saved C2 timer countdown");
			if (s->object_pointer) {
				if (a->objp != restored.at(s->object_pointer)) throw Exception("Lost saved object pointer");
				++links;
			}
			if (s->carrier && s->carrier != s) {
				if (a->carriedby != restored.at(s->carrier)) throw Exception("Lost saved carrier");
				++links;
			}
			if (auto* simple = dynamic_cast<const sfc::SimpleObjectV2*>(s)) check_entity(a.get(), 0, *simple->entity);
			if (auto* scenery = dynamic_cast<const sfc::SceneryV2*>(s)) check_entity(a.get(), 0, *scenery->entity);
			if (auto* compound = dynamic_cast<const sfc::CompoundObjectV2*>(s)) {
				for (unsigned int i = 0; i < compound->parts.size(); ++i)
					if (compound->parts[i].entity) check_entity(a.get(), i, *compound->parts[i].entity);
			}
			if (auto* bubble = dynamic_cast<const sfc::BubbleV2*>(s)) {
				auto* runtime = dynamic_cast<Bubble*>(a.get());
				const std::string text(bubble->text.begin(), std::find(bubble->text.begin(), bubble->text.end(), 0));
				if (!runtime || runtime->getText() != text) throw Exception("Lost saved speech bubble text");
				++bubbles;
			}
			if (const auto* creature = dynamic_cast<const sfc::CreatureV2*>(s)) {
				auto* holder = dynamic_cast<SkeletalCreature*>(a.get());
				if (!holder || holder->getCreature()->getAge() != creature->age_ticks || holder->getCreature()->getStage() != creature->age)
					throw Exception("Lost saved creature age");
				auto* biology = dynamic_cast<c2Creature*>(holder->getCreature());
				sfc::CreatureV2 language;
				biology->updateC2Language(language);
				for (size_t i = 0; i < creature->vocabulary.size(); ++i) {
					const auto& expected = creature->vocabulary[i];
					const auto& actual = language.vocabulary[i];
					if (expected.in != actual.in || expected.out != actual.out || expected.strength != actual.strength ||
						biology->getC2SpokenWord(i) != expected.out)
						throw Exception("Lost saved C2 vocabulary");
				}
				const unsigned foot = creature->downfoot_right ? 12 : 11;
				if (holder->x + holder->attachmentX(foot, 1) != creature->foot_x || holder->y + holder->attachmentY(foot, 1) != creature->foot_y)
					throw Exception("Lost saved creature foot position");
				++creatures;
			}
		}
		if (saved.script_state.selected_creature && world.selectedcreature != restored.at(saved.script_state.selected_creature))
			throw Exception("Lost selected creature");
		size_t macros = 0;
		for (const auto& macro : saved.script_state.macros) {
			if (!macro) continue;
			try {
				auto* vm = restored.at(macro->owner)->vm;
				if (!vm || !vm->currentscript || vm->stopped()) throw Exception("Saved macro lost its script");
				++macros;
			} catch (const std::exception& e) {
				throw Exception(fmt::format("Saved macro owner {} at {}: {}", macro->owner ? macro->owner->unid : 0, macro->instruction_pointer, e.what()));
			}
		}
		world.drawWorld(engine.camera.get(), get_backend()->getMainRenderTarget());
		fmt::print("C2 paused world reconstruction passed: {} agents, {} creatures, {} object links, {} sprite parts, {} speech bubbles; {} active macros attached without execution ({} slots)\n",
			restored.size(), creatures, links, parts, bubbles, macros, saved.script_state.macros.size());
		if (resume_ticks) {
			const auto errors_before = world.runtime_error_count;
			const auto tick_before = world.worldtickcount;
			world.paused = false;
			for (unsigned int tick = 0; tick < resume_ticks; ++tick) {
				engine.update();
				if (world.runtime_error_count != errors_before)
					throw Exception(fmt::format("Resumed C2 world reported an agent error on tick {}", tick + 1));
			}
			if (world.worldtickcount != tick_before + resume_ticks)
				throw Exception("Resumed C2 world did not advance for every requested tick");
			for (const auto& [source, agent] : restored) {
				const auto* saved_creature = dynamic_cast<const sfc::CreatureV2*>(source);
				if (!saved_creature) continue;
				auto* holder = dynamic_cast<SkeletalCreature*>(agent.get());
				if (world.lookupUNID(source->unid).get() != agent.get() || !holder)
					throw Exception("Lost a saved creature during runtime check");
				auto* creature = holder->getCreature();
				if (saved_creature->life_state == 0 && creature->isAlive() && !agent->paused && creature->getAge() <= saved_creature->age_ticks)
					throw Exception("Living saved creature did not advance");
				fmt::print("Saved creature {}: age {} -> {}, alive {} -> {}\n", saved_creature->moniker,
					saved_creature->age_ticks, creature->getAge(), saved_creature->life_state == 0, creature->isAlive());
			}
			world.drawWorld(engine.camera.get(), get_backend()->getMainRenderTarget());
			fmt::print("C2 saved-world runtime check passed: {} ticks without reported agent errors; restoration fidelity remains under development\n", resume_ticks);
		}
		restored.clear();
		engine.shutdown();
		engine_ready = false;
		return 0;
	} catch (const std::exception& e) {
		fmt::print(stderr, "C2 world reconstruction failed: {}\n", e.what());
		if (engine_ready) engine.shutdown();
		return 1;
	}
}

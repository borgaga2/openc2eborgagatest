#include "C2WorldLoader.h"
#include "C2MacroState.h"
#include "caosVM.h"
#include "caosValue.h"
#include "SFCFile.h"
#include "World.h"
#include "Engine.h"
#include "Camera.h"
#include "Map.h"
#include "Bubble.h"
#include "SpritePart.h"
#include "Agent.h"
#include "historyManager.h"
#include "creatures/SkeletalCreature.h"
#include "creatures/oldCreature.h"
#include "creatures/oldBrain.h"
#include "fileformats/sfc/C2SFCFile.h"
#include "fileformats/sfc/C2CreatureData.h"
#include "common/Exception.h"

#include <algorithm>
#include <set>

namespace {
template <typename Buffer> std::string nulString(const Buffer& bytes) {
	return std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), 0));
}

void restoreObjectState(Agent& agent, const sfc::ObjectV2& saved) {
	agent.setAttributes(saved.attributes);
	agent.actv = saved.active;
	agent.timerrate = saved.timer_interval;
	for (unsigned int i = 0; i < 100; ++i) agent.var[i].setInt(static_cast<int32_t>(saved.variables[i]));
	agent.size = saved.size; agent.thrt = saved.threat; agent.range = saved.range;
	agent.accg = saved.acceleration; agent.rest = saved.restitution; agent.aero = saved.aerodynamic_drag;
	agent.velx = saved.velocity_x; agent.vely = saved.velocity_y;
	agent.falling = saved.gravity_data != 0xffffffff;
	agent.paused = (saved.flags & 2) != 0;
}

// Reject broken cross-links before copyToWorld registers any rooms or agents.
// A call button can own a lift record outside the top-level object list; match
// object()'s construction graph rather than assuming that list is exhaustive.
void validateWorldReferences(const sfc::C2SFCFile& saved) {
	std::set<const sfc::ObjectV2*> objects;
	std::function<void(const sfc::ObjectV2*)> visit = [&](const sfc::ObjectV2* object) {
		if (!object || !objects.insert(object).second) return;
		if (const auto* button = dynamic_cast<const sfc::CallButtonV2*>(object))
			visit(button->lift.get());
	};
	for (const auto& object : saved.objects) visit(object.get());
	for (const auto& object : saved.sceneries) visit(object.get());
	const auto check = [&](const sfc::ObjectV2* object) {
		if (object && !objects.count(object))
			throw Exception("C2 snapshot refers to an object outside its world");
	};
	std::map<uint32_t, const sfc::ObjectV2*> identities;
	std::set<std::string> monikers;
	std::set<const sfc::ObjectV2*> occupied_carriers;
	for (const auto* object : objects) {
		if (object->unid && !identities.emplace(object->unid, object).second)
			throw Exception("Duplicate object identity in C2 snapshot");
		check(object->object_pointer);
		check(object->carrier);
		if (const auto* creature = dynamic_cast<const sfc::CreatureV2*>(object)) {
			if (creature->moniker.empty() || !monikers.insert(creature->moniker).second)
				throw Exception("Missing or duplicate creature identity in C2 snapshot");
		}
		std::set<const sfc::ObjectV2*> chain;
		for (auto* carrier = object; carrier; carrier = carrier->carrier) {
			check(carrier);
			// Original hands use a self-reference, not a physical carrying link.
			if (dynamic_cast<const sfc::PointerToolV2*>(carrier) && carrier->carrier == carrier) break;
			if (!chain.insert(carrier).second)
				throw Exception("Cyclic carrier links in C2 save");
		}
		if (object->carrier && object->carrier != object && !occupied_carriers.insert(object->carrier).second)
			throw Exception("Multiple held objects share one carrier in C2 snapshot");
	}
	for (const auto* object : objects) {
		if (const auto* bubble = dynamic_cast<const sfc::BubbleV2*>(object)) {
			if (!identities.count(bubble->creator_id))
				throw Exception("Saved C2 bubble has no creator");
		}
	}
	check(saved.script_state.selected_creature);
	if (saved.script_state.selected_creature && !dynamic_cast<const sfc::CreatureV2*>(saved.script_state.selected_creature))
		throw Exception("Selected C2 creature is not a creature");
	std::set<const sfc::ObjectV2*> owners;
	for (const auto& macro : saved.script_state.macros) {
		if (!macro) continue;
		check(macro->owner); check(macro->from); check(macro->target); check(macro->it);
		if (!macro->owner || macro->exec || macro->self_destruct != 1 || macro->stack_pointer > macro->stack.size() || macro->wait < 0)
			throw Exception("Unsupported C2 macro ownership or execution state");
		if (!owners.insert(macro->owner).second)
			throw Exception("Multiple active C2 macros for one owner are not supported");
	}
}

class RestoredBubble : public Bubble {
  public:
	RestoredBubble(const sfc::BubbleV2& s)
		: Bubble(s.family, s.genus, s.species, s.entity->plane, s.entity->gallery->filename,
			s.entity->gallery->absolute_base, s.entity->gallery->images.size(), 8, 8, 95, 12, 0x080810, 0xC0FFFF) {
		leftside = s.flags[0] != 0;
		timeout = s.life;
	}
};

class SavedBubble : public SFCObject {
	const sfc::BubbleV2& saved;
	Bubble* agent = nullptr;
  public:
	SavedBubble(SFCFile* file, const sfc::BubbleV2& s) : SFCObject(file), saved(s) {}
	void read() override { throw Exception("SavedBubble uses the typed C2 archive reader"); }
	Agent* copiedAgent() override { return agent; }
	void copyToWorld() override {
		if (!saved.entity || !saved.entity->gallery) throw Exception("Saved C2 bubble has no sprite");
		auto creator = world.lookupUNID(saved.creator_id);
		if (!creator) throw Exception("Saved C2 bubble has no creator");
		agent = new RestoredBubble(saved);
		agent->finishInit();
		world.setUNID(agent, saved.unid);
		restoreObjectState(*agent, saved);
		agent->floatTo(creator);
		agent->setText(nulString(saved.text));
		auto* sprite = dynamic_cast<SpritePart*>(agent->part(0));
		sprite->setBase(saved.entity->base);
		sprite->setPose(saved.entity->pose_plus_base - saved.entity->base);
		agent->moveTo(saved.entity->x, saved.entity->y);
	}
};

class SavedCreature : public SFCObject {
	const sfc::CreatureV2& saved;
	const C2WorldAssets& assets;
	SkeletalCreature* agent = nullptr;
  public:
	SavedCreature(SFCFile* file, const sfc::CreatureV2& s, const C2WorldAssets& a)
		: SFCObject(file), saved(s), assets(a) {}
	void read() override { throw Exception("SavedCreature uses the typed C2 archive reader"); }
	Agent* copiedAgent() override { return agent; }
	void copyToWorld() override {
		if (!saved.brain || !saved.biochemistry) throw Exception("Saved C2 creature is missing its brain or biochemistry");
		auto genome = assets.genome(saved.moniker);
		if (!genome || genome->getVersion() != 2) throw Exception("Missing C2 creature genome");
		auto holder = std::make_unique<SkeletalCreature>(saved.family);
		holder->setClassifier(saved.family, saved.genus, saved.species);
		auto biology = std::make_unique<c2Creature>(genome, saved.sex == 2, 0, holder.get(), false);
		biology->getBrain()->restoreC2State(*saved.brain);
		biology->restoreC2Physiology(saved);
		biology->restoreC2Language(saved);
		biology->restoreC2Biochemistry(*saved.biochemistry, saved.biotick);
		biology->restoreC2Instincts(saved);
		holder->setCreature(biology.release());
		holder->setClassifier(saved.family, saved.genus, saved.species);
		holder->restoreC2Body(saved, assets.composite);
		agent = holder.release();
		agent->Agent::finishInit(); // register without newborn gene/pose initialization
		world.setUNID(agent, saved.unid);
		restoreObjectState(*agent, saved);
		auto& history = world.history->restoreMoniker(saved.moniker, genome);
		history.moveToCreature(agent);
		history.wasBorn();
		if (!agent->getCreature()->isAlive()) history.hasDied();
		history.name = saved.history.names[0];
	}
};
}

// Adapt the decoded C2 records to the established room/object constructors.
// The archive owns all source records; SFCFile owns the temporary adapters.
class C2WorldBridge {
	SFCFile file;
	C2WorldAssets assets;
	std::map<std::string, std::shared_ptr<genomeFile>> genomes;
	std::map<std::string, std::shared_ptr<creaturesImage>> composites;
	std::map<const sfc::CGalleryV1*, CGallery*> galleries;
	std::map<const sfc::EntityV2*, SFCEntity*> entities;
	std::map<const sfc::ObjectV2*, SFCObject*> objects;
	std::vector<SFCObject*> object_order;
	void prepareCreatureAssets() {
		for (const auto& [source, adapter] : objects) {
			const auto* creature = dynamic_cast<const sfc::CreatureV2*>(source);
			if (!creature) continue;
			if (!creature->brain || !creature->biochemistry || !creature->body)
				throw Exception("Saved C2 creature is missing its brain, biochemistry or body");
			assets.genome(creature->moniker);
			const auto load_part = [&](const sfc::BodyPartV2& part) {
				if (!part.gallery) throw Exception("Saved C2 body part is missing its gallery");
				assets.composite(part.gallery->filename);
			};
			load_part(*creature->body);
			std::set<const sfc::LimbV2*> visited;
			for (auto limb : creature->limbs) {
				while (limb) {
					if (!visited.insert(limb.get()).second)
						throw Exception("Cyclic or shared limbs in C2 snapshot");
					load_part(*limb);
					limb = limb->next;
				}
			}
		}
	}
	template <typename T, typename... Args> T* make(Args&&... args) {
		auto* value = new T(&file, std::forward<Args>(args)...);
		file.storage.push_back(value);
		return value;
	}
	CGallery* gallery(const std::shared_ptr<sfc::CGalleryV1>& source) {
		if (!source) return nullptr;
		auto& result = galleries[source.get()];
		if (!result) {
			result = make<CGallery>();
			result->filename = source->filename;
			result->firstimg = source->absolute_base;
			result->noframes = source->images.size();
		}
		return result;
	}
	SFCEntity* entity(const std::shared_ptr<sfc::EntityV2>& source) {
		if (!source) return nullptr;
		auto& result = entities[source.get()];
		if (!result) {
			result = make<SFCEntity>();
			result->sprite = gallery(source->gallery);
			if (!result->sprite) throw Exception("C2 entity is missing its gallery");
			result->currframe = source->pose_plus_base; result->imgoffset = source->base;
			result->zorder = source->plane; result->x = source->x; result->y = source->y;
			result->haveanim = source->has_animation; result->animframe = source->animation_frame;
			result->animstring = nulString(source->animation);
		}
		return result;
	}
	SFCScript script(const sfc::ScriptV2& s) { return {s.genus, s.family, s.species, s.event, s.text}; }
	SFCObject* object(const sfc::ObjectV2* source) {
		if (!source) return nullptr;
		if (objects.count(source)) return objects.at(source);
		SFCObject* result = nullptr;
		if (auto* s = dynamic_cast<const sfc::CreatureV2*>(source)) result = make<SavedCreature>(*s, assets);
		else if (auto* s = dynamic_cast<const sfc::BubbleV2*>(source)) result = make<SavedBubble>(*s);
		else if (dynamic_cast<const sfc::PointerToolV2*>(source)) result = make<SFCPointerTool>();
		else if (dynamic_cast<const sfc::CallButtonV2*>(source)) result = make<SFCCallButton>();
		else if (dynamic_cast<const sfc::LiftV2*>(source)) result = make<SFCLift>();
		else if (dynamic_cast<const sfc::VehicleV2*>(source)) result = make<SFCVehicle>();
		else if (dynamic_cast<const sfc::BlackboardV2*>(source)) result = make<SFCBlackboard>();
		else if (dynamic_cast<const sfc::CompoundObjectV2*>(source)) result = make<SFCCompoundObject>();
		else if (dynamic_cast<const sfc::SimpleObjectV2*>(source)) result = make<SFCSimpleObject>();
		else if (dynamic_cast<const sfc::SceneryV2*>(source)) result = make<SFCScenery>();
		else throw Exception("Unsupported C2 object in world snapshot");
		objects[source] = result;
		object_order.push_back(result);
		result->family = source->family; result->genus = source->genus; result->species = source->species;
		result->unid = source->unid; result->attr = source->attributes; result->actv = source->active;
		result->left = source->limit.left; result->top = source->limit.top;
		result->right = source->limit.right; result->bottom = source->limit.bottom;
		result->sprite = gallery(source->gallery);
		result->tickreset = source->timer_interval; result->tickstate = source->timer_state;
		std::copy(source->variables.begin(), source->variables.end(), result->variables);
		result->currentsound = nulString(source->current_sound);
		result->size = source->size; result->threat = source->threat; result->range = source->range;
		result->accg = source->acceleration; result->rest = source->restitution; result->aero = source->aerodynamic_drag;
		result->velx = source->velocity_x; result->vely = source->velocity_y; result->gravdata = source->gravity_data;
		result->frozen = (source->flags & 2) != 0;
		for (const auto& s : source->scripts) result->scripts.push_back(script(s));
		if (auto* s = dynamic_cast<const sfc::SimpleObjectV2*>(source)) {
			auto* r = dynamic_cast<SFCSimpleObject*>(result);
			if (r) {
			r->entity = entity(s->entity); r->partzorder = s->plane;
			if (!r->entity || !r->sprite) throw Exception("C2 simple object has no entity or gallery");
			std::copy(s->click_behavior.begin(), s->click_behavior.end(), r->bhvrclick);
			r->bhvrtouch = s->touch_behavior;
			r->pickup_handles.assign(s->pickup_handles.begin(), s->pickup_handles.end());
			r->pickup_points.assign(s->pickup_points.begin(), s->pickup_points.end());
			}
		}
		if (auto* s = dynamic_cast<const sfc::CompoundObjectV2*>(source)) {
			auto* r = dynamic_cast<SFCCompoundObject*>(result);
			for (const auto& p : s->parts) { r->parts.push_back(entity(p.entity)); r->parts_relx.push_back(p.x); r->parts_rely.push_back(p.y); }
			if (r->parts.empty() || !r->parts[0]) throw Exception("C2 compound object has no main part");
			for (unsigned int i = 0; i < 6; ++i) {
				r->hotspots[i] = {s->hotspots[i].left, s->hotspots[i].top, s->hotspots[i].right, s->hotspots[i].bottom,
					s->function_hotspots[i], static_cast<uint16_t>(s->function_messages[i]), s->function_masks[i]};
			}
		}
		if (auto* s = dynamic_cast<const sfc::VehicleV2*>(source)) {
			auto* r = dynamic_cast<SFCVehicle*>(result);
			r->cabinleft = s->cabin.left; r->cabintop = s->cabin.top; r->cabinright = s->cabin.right; r->cabinbottom = s->cabin.bottom;
			r->xvec = s->velocity_x_256; r->yvec = s->velocity_y_256; r->bump = s->bump;
		}
		if (auto* s = dynamic_cast<const sfc::LiftV2*>(source)) {
			auto* r = dynamic_cast<SFCLift*>(result);
			if (s->num_floors > 8) throw Exception("Invalid saved C2 lift floor count");
			r->nobuttons = s->num_floors; r->currentbutton = s->current_floor; r->alignwithcabin = s->align_with_cabin;
			std::copy(s->floors.begin(), s->floors.end(), r->callbuttony);
		}
		if (auto* s = dynamic_cast<const sfc::CallButtonV2*>(source)) {
			auto* r = dynamic_cast<SFCCallButton*>(result);
			r->ourLift = dynamic_cast<SFCLift*>(object(s->lift.get())); r->liftid = s->floor;
			if (!r->ourLift) throw Exception("Saved C2 call button has no lift");
		}
		if (auto* s = dynamic_cast<const sfc::BlackboardV2*>(source)) {
			auto* r = dynamic_cast<SFCBlackboard*>(result);
			r->textx = s->text_x; r->texty = s->text_y;
			r->backgroundcolour = s->background_color; r->chalkcolour = s->chalk_color; r->aliascolour = s->alias_color;
			for (const auto& w : s->words) r->strings.push_back({w.value, nulString(w.text)});
		}
		if (auto* s = dynamic_cast<const sfc::SceneryV2*>(source)) dynamic_cast<SFCScenery*>(result)->entity = entity(s->entity);
		return result;
	}
  public:
	C2WorldBridge(const C2WorldAssets& a) {
		file.setVersion(1); file.restoring_snapshot = true;
		assets.genome = [this, load = a.genome](const std::string& moniker) {
			auto found = genomes.find(moniker);
			if (found != genomes.end()) return found->second;
			if (!load) throw Exception("Missing C2 genome resolver");
			auto genome = load(moniker);
			if (!genome || genome->getVersion() != 2) throw Exception("Missing C2 creature genome");
			genomes.emplace(moniker, genome);
			return genome;
		};
		assets.composite = [this, load = a.composite](const std::string& name) {
			auto found = composites.find(name);
			if (found != composites.end()) return found->second;
			if (!load) throw Exception("Missing C2 composite sprite resolver");
			auto image = load(name);
			if (!image) throw Exception("Missing C2 composite sprite");
			composites.emplace(name, image);
			return image;
		};
	}
	std::map<const sfc::ObjectV2*, AgentRef> load(const sfc::C2SFCFile& saved) {
		if (engine.version != 2 || world.map->getMetaRoomCount() || !world.agents.empty())
			throw Exception("C2 snapshot reconstruction requires an empty C2 world");
		if (!saved.map || !saved.map->background) throw Exception("Missing saved C2 map");
		validateWorldReferences(saved);
		file.mapdata = make<MapData>();
		file.mapdata->background = gallery(saved.map->background);
		for (const auto& s : saved.map->rooms) {
			if (!s) continue;
			auto* r = make<CRoom>(); file.mapdata->rooms.push_back(r);
			r->id = s->id; r->left = s->left; r->top = s->top; r->right = s->right; r->bottom = s->bottom; r->roomtype = s->type;
			r->floorvalue = s->floor_value; r->floorpoints = s->floor_points; r->dropstatus = s->drop_status; r->music = s->music;
			r->inorganicnutrients = s->inorganic_nutrients; r->organicnutrients = s->organic_nutrients;
			r->temperature = s->temperature; r->pressure = s->pressure; r->lightlevel = s->light; r->radiation = s->radiation;
			r->heatsource = s->heat_source; r->pressuresource = s->pressure_source; r->lightsource = s->light_source; r->radiationsource = s->radiation_source;
			r->windx = s->wind_x; r->windy = s->wind_y; r->reserved_after_floorpoints = s->reserved;
			for (unsigned int i = 0; i < 4; ++i) for (const auto& door : s->doors[i]) {
				if (!door) throw Exception("Null door in saved C2 room");
				auto* d = make<CDoor>(); d->openness = door->openness; d->otherroom = door->other_room; r->doors[i].push_back(d);
			}
		}
		for (const auto& s : saved.scripts) file.scripts.push_back(script(s));
		for (const auto& s : saved.objects) object(s.get());
		for (const auto& s : saved.sceneries) object(s.get());
		// Resolve every creature dependency before the first live-world change.
		// Keep these exact objects for construction; do not reopen assets later.
		prepareCreatureAssets();
		// Lifts must exist before their call buttons bind to them.
		for (auto* r : object_order) if (dynamic_cast<SFCLift*>(r)) file.objects.push_back(r);
		for (auto* r : object_order) if (!dynamic_cast<SFCLift*>(r) && !dynamic_cast<SavedBubble*>(r)) file.objects.push_back(r);
		for (auto* r : object_order) if (dynamic_cast<SavedBubble*>(r)) file.objects.push_back(r);
		file.scrollx = saved.script_state.scroll_x; file.scrolly = saved.script_state.scroll_y;
		world.paused = true;
		file.copyToWorld(); // attach suspended macros only after all agents exist
		std::map<const sfc::ObjectV2*, AgentRef> result;
		for (const auto& [s, r] : objects) result.emplace(s, r->copiedAgent());
		for (const auto& [s, a] : result) {
			objects.at(s)->copyTimerToAgent(*a);
			if (s->object_pointer) a->objp = result.at(s->object_pointer);
			if (s->carrier && s->carrier != s) {
				a->carriedby = result.at(s->carrier);
				a->carriedby->carrying = a;
			}
		}
		for (const auto& [s, a] : result) a->setZOrder(a->zorder);
		if (saved.script_state.selected_creature) world.selectedcreature = result.at(saved.script_state.selected_creature);
		// Prepare every suspended script before attaching any of them. No
		// fireScript/queueScript call may restart native scripts during loading.
		std::vector<std::pair<Agent*, std::unique_ptr<caosVM>>> macros;
		std::set<const sfc::ObjectV2*> owners;
		for (const auto& macro : saved.script_state.macros) {
			if (!macro) continue;
			if (!owners.insert(macro->owner).second)
				throw Exception("Multiple active C2 macros for one owner are not supported");
			auto vm = prepareC2Macro(*macro, [&](const sfc::ObjectV2* object) {
				const auto found = result.find(object);
				return found == result.end() ? AgentRef() : found->second;
			});
			auto* owner = vm->owner.get();
			if (owner->vm) throw Exception("C2 macro owner already has a running script");
			macros.emplace_back(owner, std::move(vm));
		}
		for (auto& [owner, vm] : macros) owner->pushVM(vm.release());
		return result;
	}
};

std::map<const sfc::ObjectV2*, AgentRef> loadC2WorldSnapshot(const sfc::C2SFCFile& saved, const C2WorldAssets& assets) {
	return C2WorldBridge(assets).load(saved);
}

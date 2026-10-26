#include "oldCreature.h"
#include "common/Exception.h"
#include "fileformats/sfc/C2BiochemistryData.h"
#include "fileformats/sfc/C2BrainData.h"

#include <unordered_map>

namespace {

std::shared_ptr<sfc::COrganV2> copy_organ(const sfc::COrganV2* organ) {
	if (auto* brain = dynamic_cast<const sfc::CBrainV2*>(organ))
		return std::make_shared<sfc::CBrainV2>(*brain);
	return organ ? std::make_shared<sfc::COrganV2>(*organ) : std::make_shared<sfc::COrganV2>();
}

} // namespace

void c2Creature::restoreC2Biochemistry(const sfc::CBiochemistryV2& saved, uint32_t biotick) {
	if (saved.stored_organ_count > saved.organs.size())
		throw Exception("Saved C2 biochemistry has too many organs");
	C2ReceptorStateContext context;
	std::unordered_map<const sfc::COrganV2*, std::shared_ptr<c2Organ>> restored;
	std::vector<std::shared_ptr<c2Organ>> prepared(saved.organs.size());
	std::vector<std::shared_ptr<const sfc::COrganV2>> retained(saved.organs.begin(), saved.organs.end());
	for (size_t i = 0; i < saved.organs.size(); ++i) {
		const auto& organ = saved.organs[i];
		if (!organ)
			continue;
		auto& instance = restored[organ.get()];
		if (!instance)
			instance = std::make_shared<c2Organ>(this, *organ, &context);
		prepared[i] = instance;
	}
	while (!prepared.empty() && !prepared.back())
		prepared.pop_back();
	// All allocation, validation and binding finishes before changing the
	// creature's existing chemistry. Loading does not run initialization genes.
	organs.swap(prepared);
	retained_organs.swap(retained);
	for (size_t i = 0; i < saved.chemicals.size(); ++i) {
		chemicals[i] = saved.chemicals[i].concentration;
		chemical_decay[i] = saved.chemicals[i].decay;
	}
	biochemticks = biotick;
	stored_organ_count = saved.stored_organ_count;
	retained_biochemistry_state = saved.reserved_after_organs;
}

void c2Creature::updateC2Biochemistry(sfc::CBiochemistryV2& saved) const {
	if (organs.size() > saved.organs.size())
		throw Exception("C2 creature has too many organs to save");
	auto captured = saved;
	captured.organs.fill(nullptr);
	C2ReceptorStateContext context;
	std::unordered_map<const c2Organ*, std::shared_ptr<sfc::COrganV2>> records;
	for (size_t i = 0; i < organs.size(); ++i) {
		if (!organs[i])
			continue;
		auto& record = records[organs[i].get()];
		if (!record) {
			record = copy_organ(i < retained_organs.size() ? retained_organs[i].get() : nullptr);
			organs[i]->updateC2State(*record, &context);
		}
		captured.organs[i] = record;
	}
	for (size_t i = 0; i < captured.chemicals.size(); ++i) {
		captured.chemicals[i].concentration = chemicals[i];
		captured.chemicals[i].decay = chemical_decay[i];
	}
	captured.stored_organ_count = stored_organ_count;
	captured.reserved_after_organs = retained_biochemistry_state;
	saved = std::move(captured);
}

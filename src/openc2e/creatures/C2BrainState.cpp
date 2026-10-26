#include "oldBrain.h"
#include "oldCreature.h"
#include "common/Exception.h"
#include "fileformats/sfc/C2BrainData.h"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>

namespace {

// C2 lobe state contains a 31-byte header followed by two 108-byte dendrite
// descriptions. Each saved SVRule occupies 15 bytes: 12 instructions, its
// random constant, and two retained bytes not yet used by this runtime.
uint32_t read_u32(const std::array<uint8_t, 247>& state, size_t offset) {
	uint32_t result = 0;
	for (unsigned int i = 0; i < 4; ++i)
		result |= static_cast<uint32_t>(state[offset + i]) << (8 * i);
	return result;
}

void read_dendrite_gene(oldDendriteInfo& gene, const std::array<uint8_t, 247>& state, size_t offset) {
	gene.srclobe = numeric_cast<uint8_t>(read_u32(state, offset));
	uint8_t* values[] = {&gene.min, &gene.max, &gene.spread, &gene.fanout,
		&gene.minLTW, &gene.maxLTW, &gene.minstr, &gene.maxstr, &gene.migrateflag,
		&gene.relaxsuscept, &gene.relaxSTW, &gene.LTWgainrate, &gene.strgain, &gene.strloss};
	for (size_t i = 0; i < std::size(values); ++i)
		*values[i] = state[offset + 4 + i];
	uint8_t* rules[] = {gene.strgainrule, gene.strlossrule, gene.susceptrule,
		gene.relaxrule, gene.backproprule, gene.forproprule};
	for (size_t i = 0; i < std::size(rules); ++i)
		std::copy_n(state.begin() + offset + 18 + i * 15, 12, rules[i]);
}

} // namespace

void oldBrain::restoreC2State(const sfc::CBrainV2& state) {
	if (parent->getGenome()->getVersion() != 2)
		throw Exception("Cannot restore C2 state into a different brain version");
	if (state.lobes.size() != lobes.size() || state.processing_order.size() != lobes.size())
		throw Exception("Saved C2 brain does not match its genome's lobes");
	auto order = state.processing_order;
	auto sorted_order = order;
	std::sort(sorted_order.begin(), sorted_order.end());
	for (size_t i = 0; i < sorted_order.size(); ++i)
		if (sorted_order[i] != i)
			throw Exception("Saved C2 brain has an invalid processing order");

	struct LobeState {
		std::unique_ptr<oldBrainLobeGene> gene;
		std::vector<oldNeuron> neurons;
		std::vector<unsigned int> active;
	};
	std::vector<LobeState> restored(lobes.size());
	size_t total = 0;
	for (size_t i = 0; i < lobes.size(); ++i) {
		const auto& saved = state.lobes[i];
		if (!lobes[i] || !saved.width || !saved.height || saved.num_neurons > 65536 ||
			static_cast<uint64_t>(saved.width) * saved.height != saved.num_neurons ||
			total > state.neurons.size() || saved.num_neurons > state.neurons.size() - total)
			throw Exception("Saved C2 lobe has invalid neuron dimensions");
		total += saved.num_neurons;
		if (total > 1048576)
			throw Exception("Saved C2 brain has too many neurons");
		auto gene = std::make_unique<oldBrainLobeGene>(*lobes[i]->ourGene);
		gene->perceptflag = numeric_cast<uint8_t>(read_u32(saved.state, 0));
		gene->nominalthreshold = saved.state[11];
		gene->leakagerate = saved.state[12];
		gene->reststate = saved.state[13];
		gene->inputgain = saved.state[14];
		std::copy_n(saved.state.begin() + 15, 12, gene->staterule);
		gene->flags = saved.state[30];
		read_dendrite_gene(gene->dendrite1, saved.state, 31);
		read_dendrite_gene(gene->dendrite2, saved.state, 139);
		if (gene->dendrite1.srclobe >= lobes.size() || gene->dendrite2.srclobe >= lobes.size())
			throw Exception("Saved C2 brain references a missing source lobe");
		restored[i].gene = std::move(gene);
		restored[i].neurons.resize(saved.num_neurons);
		restored[i].active.reserve(saved.num_neurons);
	}
	size_t perceived = 0;
	for (size_t i = 1; i < restored.size(); ++i)
		if (restored[i].gene->perceptflag)
			perceived += restored[i].neurons.size();
	if (restored.empty() || perceived > restored[0].neurons.size())
		throw Exception("Saved C2 perception lobe cannot hold its input lobes");
	if (total != state.neurons.size())
		throw Exception("Saved C2 brain has extra neurons outside its lobe table");

	size_t index = 0;
	for (size_t i = 0; i < lobes.size(); ++i) {
		size_t dendrites = 0;
		const unsigned int sources[] = {restored[i].gene->dendrite1.srclobe, restored[i].gene->dendrite2.srclobe};
		for (size_t n = 0; n < restored[i].neurons.size(); ++n) {
			const auto& saved = state.neurons[index++];
			auto& neuron = restored[i].neurons[n];
			neuron.output = saved.state[2]; neuron.state = saved.state[3];
			neuron.wta_disable = saved.state[4]; neuron.percept_src = saved.state[5];
			neuron.leakin = saved.state[6]; neuron.leakout = saved.state[7];
			if (neuron.state >= restored[i].gene->nominalthreshold)
				restored[i].active.push_back(numeric_cast<unsigned int>(n));
			const std::vector<sfc::DendriteV2>* saved_dendrites[] = {&saved.dendrites0, &saved.dendrites1};
			for (unsigned int type = 0; type < 2; ++type) {
				if (saved_dendrites[type]->size() > 255)
					throw Exception("Saved C2 neuron has too many dendrites");
				dendrites += saved_dendrites[type]->size();
				for (const auto& saved_dendrite : *saved_dendrites[type]) {
					if (saved_dendrite.source_index >= restored[sources[type]].neurons.size())
						throw Exception("Saved C2 dendrite references a missing source neuron");
					oldDendrite dendrite;
					dendrite.src = &restored[sources[type]].neurons[saved_dendrite.source_index];
					dendrite.home_x = saved_dendrite.home_x; dendrite.home_y = saved_dendrite.home_y;
					dendrite.suscept = saved_dendrite.susceptibility;
					dendrite.stw = saved_dendrite.short_term_weight;
					dendrite.ltw = saved_dendrite.long_term_weight;
					dendrite.strength = saved_dendrite.strength;
					neuron.dendrites[type].push_back(dendrite);
				}
			}
		}
		if (dendrites != state.lobes[i].num_dendrites)
			throw Exception("Saved C2 brain has an inconsistent dendrite total");
	}

	// Nothing above changes the running brain. Swapping the prepared vectors
	// preserves their neuron addresses and commits all reconstructed links.
	for (size_t i = 0; i < lobes.size(); ++i) {
		auto& lobe = *lobes[i];
		const auto& saved = state.lobes[i];
		lobe.restored_gene = std::move(restored[i].gene);
		lobe.ourGene = lobe.restored_gene.get();
		lobe.neurons.swap(restored[i].neurons);
		lobe.active_neurons.swap(restored[i].active);
		lobe.width = saved.width; lobe.height = saved.height;
		lobe.lobe_activity = saved.state[4];
		std::copy_n(saved.state.begin() + 5, 6, lobe.chems);
		lobe.threshold = saved.state[11]; lobe.leakagerate = saved.state[12]; lobe.inputgain = saved.state[14];
		lobe.staterule.init(1, lobe.ourGene->staterule);
		lobe.staterule.rndconst = saved.state[27];
		oldDendriteInfo* genes[] = {&lobe.ourGene->dendrite1, &lobe.ourGene->dendrite2};
		for (unsigned int type = 0; type < 2; ++type) {
			oldSVRule* rules[] = {&lobe.strgainrule[type], &lobe.strlossrule[type], &lobe.susceptrule[type],
				&lobe.relaxrule[type], &lobe.backproprule[type], &lobe.forproprule[type]};
			uint8_t* code[] = {genes[type]->strgainrule, genes[type]->strlossrule, genes[type]->susceptrule,
				genes[type]->relaxrule, genes[type]->backproprule, genes[type]->forproprule};
			for (size_t r = 0; r < std::size(rules); ++r) {
				rules[r]->init(1, code[r]);
				rules[r]->rndconst = saved.state[31 + type * 108 + 18 + r * 15 + 12];
			}
			lobe.sourceLobe[type] = lobes[genes[type]->srclobe];
			lobe.loose_dendrites[type] = 0;
			lobe.last_loose_neuron[type] = std::numeric_limits<unsigned int>::max();
			lobe.loose_neuron_upperbound[type] = std::numeric_limits<unsigned int>::max();
		}
		lobe.inited = true;
	}
	lobe_process_order.swap(order);
	ticks = state.ticks;
}

void oldBrain::updateC2State(sfc::CBrainV2& state) const {
	if (parent->getGenome()->getVersion() != 2 || state.lobes.size() != lobes.size())
		throw Exception("Cannot update a saved record for a different C2 brain");
	auto result = state;
	std::vector<std::unordered_map<const oldNeuron*, uint32_t>> indices(lobes.size());
	size_t total = 0;
	for (size_t i = 0; i < lobes.size(); ++i) {
		if (result.lobes[i].num_neurons != lobes[i]->neurons.size())
			throw Exception("Saved C2 brain has a different neuron layout");
		for (size_t n = 0; n < lobes[i]->neurons.size(); ++n)
			indices[i].emplace(&lobes[i]->neurons[n], numeric_cast<uint32_t>(n));
		total += lobes[i]->neurons.size();
	}
	if (result.neurons.size() != total)
		throw Exception("Saved C2 brain has a different neuron total");
	size_t index = 0;
	for (size_t i = 0; i < lobes.size(); ++i) {
		const auto& lobe = *lobes[i];
		auto& saved_lobe = result.lobes[i];
		saved_lobe.state[4] = lobe.lobe_activity;
		std::copy_n(lobe.chems, 6, saved_lobe.state.begin() + 5);
		saved_lobe.state[11] = lobe.threshold;
		saved_lobe.state[12] = lobe.leakagerate;
		saved_lobe.state[14] = lobe.inputgain;
		saved_lobe.state[27] = lobe.staterule.rndconst;
		const unsigned int sources[] = {lobe.ourGene->dendrite1.srclobe, lobe.ourGene->dendrite2.srclobe};
		size_t count = 0;
		for (const auto& neuron : lobe.neurons) {
			auto& saved = result.neurons[index++];
			saved.state[2] = neuron.output; saved.state[3] = neuron.state;
			saved.state[4] = neuron.wta_disable; saved.state[5] = neuron.percept_src;
			saved.state[6] = neuron.leakin; saved.state[7] = neuron.leakout;
			std::vector<sfc::DendriteV2>* dendrites[] = {&saved.dendrites0, &saved.dendrites1};
			for (unsigned int type = 0; type < 2; ++type) {
				if (sources[type] >= indices.size() || neuron.dendrites[type].size() != dendrites[type]->size())
					throw Exception("C2 brain dendrite layout changed since loading");
				for (size_t d = 0; d < neuron.dendrites[type].size(); ++d) {
					const auto& dendrite = neuron.dendrites[type][d];
					const auto source = indices[sources[type]].find(dendrite.src);
					if (source == indices[sources[type]].end())
						throw Exception("C2 brain dendrite is outside its source lobe");
					auto& target = (*dendrites[type])[d];
					target.source_index = source->second;
					target.home_x = dendrite.home_x; target.home_y = dendrite.home_y;
					target.susceptibility = dendrite.suscept;
					target.short_term_weight = dendrite.stw; target.long_term_weight = dendrite.ltw;
					target.strength = dendrite.strength;
				}
				count += neuron.dendrites[type].size();
			}
		}
		saved_lobe.num_dendrites = numeric_cast<uint32_t>(count);
	}
	result.processing_order = lobe_process_order;
	result.ticks = ticks;
	state = std::move(result);
}

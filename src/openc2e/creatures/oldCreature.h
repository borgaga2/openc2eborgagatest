/*
 *  oldCreature.h
 *  openc2e
 *
 *  Created by Alyssa Milburn on Sat May 15 2010.
 *  Copyright (c) 2004-2010 Alyssa Milburn. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 */

#ifndef __OLDCREATURE_H
#define __OLDCREATURE_H

#include "Creature.h"

#include <array>
#include <cassert>
#include <memory>
#include <unordered_map>

struct C2SpeechResources;

namespace sfc {
struct COrganV2;
struct CBiochemistryV2;
struct ReceptorV2;
struct CreatureV2;
struct CInstinctV1;
}

// Share this context across organs when restoring/capturing a complete graph.
struct C2ReceptorStateContext {
	std::unordered_map<const sfc::ReceptorV2*, std::shared_ptr<uint8_t>> restored;
	std::unordered_map<const uint8_t*, std::shared_ptr<sfc::ReceptorV2>> captured;
};

// c1/c2

class oldCreature : public Creature {
  protected:
	// biochemistry
	unsigned char chemicals[256];

	class oldBrain* brain;

	unsigned int biochemticks;
	bioHalfLivesGene* halflives;

	// loci
	unsigned char floatingloci[8];
	unsigned char muscleenergy;
	unsigned char lifestageloci[7];
	unsigned char fertile, receptive, pregnant;
	unsigned char dead;
	unsigned char involaction[8];
	unsigned int involactionlatency[8];
	unsigned int dreamInstinctsRemaining;
	unsigned int dreamTicksRemaining;

	void addGene(gene*);
	void tickBrain();
	virtual void tickBiochemistry();

	oldCreature(std::shared_ptr<genomeFile> g, bool is_female, unsigned char _variant, CreatureAgent* a);

	void processGenes();

	AgentRef selectRepresentativeAgent(int type, std::vector<AgentRef> possibles);

  public:
	~oldCreature() override;

	// TODO: inline?
	unsigned int calculateTickMask(unsigned char);
	unsigned int calculateMultiplier(unsigned char);

	void addChemical(unsigned char id, unsigned char val);
	void subChemical(unsigned char id, unsigned char val);
	unsigned char getChemical(unsigned char id) { return chemicals[id]; }

	// TODO: is it really worth having drives outside oldCreature?
	virtual unsigned char getDrive(unsigned int id) = 0;

	void handleStimulus(unsigned int id);
	void handleStimulus(unsigned int input, unsigned int intensity, const int chemicals[4], const int amounts[4]);
	void nudgeDecisionImportance(int nudge);
	void finishDecisionAction() override;

	oldBrain* getBrain() { return brain; }
	virtual unsigned int processInstincts(unsigned int count);
	void startDreaming(unsigned int maxInstincts);

	unsigned char* getLocusPointer(bool receptor, unsigned char o, unsigned char t, unsigned char l);

	void setInvolActionLatency(unsigned int id, unsigned int n) {
		assert(id < 8);
		involactionlatency[id] = n;
	}
};

// c1

struct c1Reaction {
	explicit c1Reaction(bioReactionGene*);
	bioReactionGene* data;
};

struct c1Receptor {
	c1Receptor(bioReceptorGene*, class c1Creature*);
	bioReceptorGene* data;
	unsigned char* locus;
};

struct c1Emitter {
	c1Emitter(bioEmitterGene*, class c1Creature*);
	bioEmitterGene* data;
	unsigned char* locus;
};

class c1Creature : public oldCreature {
  protected:
	std::vector<std::shared_ptr<c1Reaction> > reactions;
	std::vector<c1Receptor> receptors;
	std::vector<c1Emitter> emitters;

	// loci
	unsigned char senses[6];
	unsigned char gaitloci[8];
	unsigned char drives[16];

	void addGene(gene*);
	void tickBiochemistry();
	void processReaction(c1Reaction&);
	void processEmitter(c1Emitter&);
	void processReceptor(c1Receptor&);

  public:
	c1Creature(std::shared_ptr<genomeFile> g, bool is_female, unsigned char _variant, CreatureAgent* a);

	void tick();

	unsigned char getDrive(unsigned int id) {
		assert(id < 16);
		return drives[id];
	}

	unsigned char* getLocusPointer(bool receptor, unsigned char o, unsigned char t, unsigned char l);

	unsigned int getGait();
};

// c2

struct c2Reaction {
	explicit c2Reaction(bioReactionGene*);
	explicit c2Reaction(const std::array<uint8_t, 9>&);
	std::array<uint8_t, 9> parameters() const;
	uint8_t reactant[4], quantity[4], rate;
};

struct c2Receptor {
	c2Receptor(bioReceptorGene*, class c2Organ*);
	c2Receptor(const std::array<uint8_t, 8>&, class c2Organ*);
	std::array<uint8_t, 8> parameters() const;
	uint8_t organ, tissue, locus_id, chemical, threshold, nominal, gain, flags;
	unsigned char* locus;
};

struct c2Emitter {
	c2Emitter(bioEmitterGene*, class c2Organ*);
	c2Emitter(const std::array<uint8_t, 8>&, class c2Organ*);
	std::array<uint8_t, 8> parameters() const;
	uint8_t organ, tissue, locus_id, chemical, threshold, rate, gain, flags;
	unsigned char* locus;
};

class c2Organ {
  protected:
	friend struct c2Receptor;
	friend struct c2Emitter;

	class c2Creature* parent;

	std::vector<std::shared_ptr<c2Reaction> > reactions;
	std::vector<c2Receptor> receptors;
	std::vector<c2Emitter> emitters;
	std::vector<std::vector<size_t>> receptor_groups;
	std::vector<std::shared_ptr<uint8_t>> receptor_identities;
	size_t processedGeneCount = 0;

	// data
	unsigned int energycost, atpdamagecoefficient;
	uint32_t identity = 0;

	// variables
	float lifeforce, shorttermlifeforce, longtermlifeforce;

	// locuses
	unsigned char damagerate, repairrate, clockrate, injurytoapply;
	unsigned char cached_health;
	uint32_t energy_gate;

	unsigned int biotick, biochemticks;

	void processReaction(c2Reaction&);
	void processEmitter(c2Emitter&);
	void processReceptors();
	void groupReceptors();

	unsigned char* getLocusPointer(bool receptor, unsigned char o, unsigned char t, unsigned char l);

  public:
	c2Organ(c2Creature* p, organGene* g);
	c2Organ(c2Creature* p, const sfc::COrganV2&, C2ReceptorStateContext* = nullptr);
	void tick();

	void processGenes();

	unsigned char getClockRate() { return clockrate; }
	unsigned char getRepairRate() { return repairrate; }
	unsigned char getDamageRate() { return damagerate; }
	unsigned int getEnergyCost() { return energycost; }
	unsigned char getInjuryToApply() { return injurytoapply; }
	float getInitialLifeforce() { return lifeforce; }
	float getShortTermLifeforce() { return shorttermlifeforce; }
	float getLongTermLifeforce() { return longtermlifeforce; }
	unsigned int getATPDamageCoefficient() { return atpdamagecoefficient; }

	size_t getReceptorCount() { return receptors.size(); }
	size_t getEmitterCount() { return emitters.size(); }
	size_t getReactionCount() { return reactions.size(); }

	void applyInjury(float);
	void restoreC2State(const sfc::COrganV2&, C2ReceptorStateContext* = nullptr);
	void updateC2State(sfc::COrganV2&, C2ReceptorStateContext* = nullptr) const;

	std::vector<gene*> genes;
};

class c2Creature : public oldCreature {
  protected:
	// biochemistry
	std::vector<std::shared_ptr<c2Organ> > organs;
	std::array<uint8_t, 256> chemical_decay{};
	uint32_t stored_organ_count = 0;
	uint8_t retained_biochemistry_state = 0;
	std::vector<std::shared_ptr<const sfc::COrganV2>> retained_organs;

	// loci
	unsigned char senses[14];
	unsigned char gaitloci[16];
	unsigned char drives[17];
	unsigned char mutationchance, mutationdegree;
	unsigned char fertility_output = 0, life_state_signal = 0;
	unsigned char retained_asleep_state = 0;
	std::deque<std::shared_ptr<sfc::CInstinctV1>> restored_instincts;
	struct C2Word {
		std::string input, output;
		uint32_t strength = 0;
	};
	std::array<C2Word, 86> vocabulary;
	int speech_noun = -1, speech_verb = -1;
	uint8_t speech_strength = 0, speech_ticks = 0;
	void applyC2SpeechCommand(int noun, int verb, unsigned noun_strength, unsigned verb_strength);

	void addGene(gene*);
	void tickBiochemistry();
	void processGenes();
	void updateFertilitySignal();

  public:
	c2Creature(std::shared_ptr<genomeFile> g, bool is_female, unsigned char _variant, CreatureAgent* a, bool initialize_language = true);
	void initializeC2Language(const C2SpeechResources&, uint32_t& random_state);
	void restoreC2Biochemistry(const sfc::CBiochemistryV2&, uint32_t biotick);
	void updateC2Biochemistry(sfc::CBiochemistryV2&) const;
	uint32_t getBiochemistryTicks() const { return biochemticks; }
	void restoreC2Language(const sfc::CreatureV2&);
	void updateC2Language(sfc::CreatureV2&) const;
	std::string getC2SpokenWord(int word) const;
	int findC2Word(const std::string& text) const;
	bool hearC2Speech(Agent* source, const std::string& text);
	void advanceC2SpeechCommand();
	bool learnC2Word(int word, const std::string& input,
		const std::vector<std::pair<std::string, std::string>>& pronunciation_rules);
	bool reinforceC2Word(int word, const std::string& input,
		const std::vector<std::pair<std::string, std::string>>& pronunciation_rules);
	void restoreC2Physiology(const sfc::CreatureV2&);
	void updateC2Physiology(sfc::CreatureV2&) const;
	void restoreC2Instincts(const sfc::CreatureV2&);
	void updateC2Instincts(sfc::CreatureV2&) const;
	size_t getNoUnprocessedInstincts() override { return restored_instincts.size() + unprocessedinstincts.size(); }
	unsigned int processInstincts(unsigned int count) override;

	void tick();

	unsigned char getDrive(unsigned int id) {
		assert(id < 17);
		return drives[id];
	}

	unsigned int noOrgans() { return organs.size(); }
	std::shared_ptr<c2Organ> getOrgan(unsigned int i) {
		assert(i < organs.size());
		return organs[i];
	}

	unsigned char* getLocusPointer(bool receptor, unsigned char o, unsigned char t, unsigned char l);

	unsigned int getGait();
};

#endif

/* vim: set noet: */

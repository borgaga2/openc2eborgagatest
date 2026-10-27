/*
 *  oldBrain.h
 *  openc2e
 *
 *  Created by Alyssa Milburn on Mon Aug 13 2007.
 *  Copyright (c) 2007 Alyssa Milburn. All rights reserved.
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

#ifndef __OLDBRAIN_H
#define __OLDBRAIN_H

#include "fileformats/genomeFile.h"

#include <map>
#include <memory>
#include <stdint.h>
#include <set>

namespace sfc {
struct CBrainV2;
struct CInstinctV1;
}

struct oldSVRule {
	unsigned int length;
	uint8_t rndconst;
	uint8_t rules[12];
	void init(uint8_t version, uint8_t* src);
};

struct oldDendrite {
	struct oldNeuron* src;
	unsigned char strength, stw, ltw, suscept;
	unsigned char home_x = 0, home_y = 0;
};

struct oldNeuron {
	unsigned char state, output, leakin, leakout;
	std::vector<oldDendrite> dendrites[2];
	unsigned char percept_src;
	unsigned char wta_disable = 0;
	oldNeuron() {
		state = 0;
		output = 0;
		leakin = 0;
		leakout = 0;
		percept_src = 0;
	}
	unsigned int magicalHash(unsigned int type);
};

class oldLobe {
	friend class oldBrain;

  protected:
	class oldBrain* parent;
	oldBrainLobeGene* ourGene;
	std::unique_ptr<oldBrainLobeGene> restored_gene;
	std::vector<oldNeuron> neurons;
	oldLobe* sourceLobe[2];
	bool inited;

	unsigned int width, height;

	unsigned char threshold, leakagerate, inputgain;
	unsigned char chems[6];
	unsigned char loose_dendrites[2];
	unsigned char lobe_activity;
	std::vector<unsigned int> active_neurons;

	// data for "all dendrites loose" migration
	unsigned int loose_neuron_upperbound[2];
	unsigned int last_loose_neuron[2];

	oldSVRule staterule;
	oldSVRule strgainrule[2], strlossrule[2];
	oldSVRule susceptrule[2];
	oldSVRule relaxrule[2];
	oldSVRule backproprule[2], forproprule[2];

	unsigned char evaluateSVRuleConstant(oldNeuron* cell, oldDendrite* dend, uint8_t id, oldSVRule& rule);
	unsigned char processSVRule(oldNeuron* cell, oldDendrite* dend, oldSVRule& rule);

	unsigned char dendrite_sum(oldNeuron& cell, unsigned int type, bool only_if_all_firing);

	bool migrationTryCandidateSlice(unsigned int type, oldLobe* src, std::vector<oldDendrite>& dendrites, unsigned int i);
	void neuronTryAllLooseMigration(unsigned int type, oldNeuron& neu);
	void neuronTryMigration(unsigned int type, oldNeuron& neu, oldLobe* src);
	void connectDendrite(unsigned int type, oldDendrite& dend, oldNeuron* dest);

	void tickDendrites(unsigned int id, unsigned int type);

  public:
	oldLobe(class oldBrain* b, oldBrainLobeGene* g);
	bool wasInited() { return inited; }
	void ensure_minimum_size(unsigned int size);
	void tick();
	void init();
	void connect();
	void wipe();

	oldBrainLobeGene* getGene() { return ourGene; }

	unsigned int getNoNeurons() { return neurons.size(); }
	oldNeuron* getNeuron(unsigned int i) { return &neurons[i]; }
	int getWinnerNeuron();
	unsigned int getDendriteCount();
	unsigned int getWidth() { return width; }
	unsigned int getHeight() { return height; }

	unsigned char* getChemPointer(unsigned int chemid) { return &chems[chemid]; }
	unsigned char* getThresholdPointer() { return &threshold; }
	unsigned char* getLeakageRatePointer() { return &leakagerate; }
	unsigned char* getInputGainPointer() { return &inputgain; }
	unsigned char* getLooseDendsPointer(unsigned int type) { return &loose_dendrites[type]; }
	unsigned char* getLobeActivityPointer() { return &lobe_activity; }
};

class oldBrain {
  protected:
	bool wireInstinct(const sfc::CInstinctV1&);
	class oldCreature* parent;
	unsigned int ticks;
	std::vector<unsigned int> lobe_process_order;

  public:
	std::vector<oldLobe*> lobes;

	oldBrain(oldCreature* p);
	~oldBrain();
	void tick();
	void processGenes();
	void init();

	// Restore into an existing C2 brain without reconnecting or ticking it.
	void restoreC2State(const sfc::CBrainV2&);
	// Update a loaded record, retaining state the runtime does not yet decode.
	void updateC2State(sfc::CBrainV2&) const;

	// False means C2 must keep the instinct queued and retry it later.
	bool processInstinct(const creatureInstinctGene& instinct);
	sfc::CInstinctV1 expressInstinct(const creatureInstinctGene&) const;
	bool processC2Instinct(const sfc::CInstinctV1&);

	oldLobe* getLobeByTissue(unsigned int id);
	oldCreature* getParent() { return parent; }
	unsigned int getTicks() { return ticks; }
};

#endif

/* vim: set noet: */

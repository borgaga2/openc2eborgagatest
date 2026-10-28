#pragma once

#include "CompoundPart.h"
#include "bytestring.h"
#include <cstdint>
#include <vector>

class AnimatablePart : public CompoundPart {
  protected:
	unsigned int frameno;
	AnimatablePart(Agent* p, unsigned int _id, int _x, int _y, int _z)
		: CompoundPart(p, _id, _x, _y, _z) { frameno = 0; }

	void updateAnimation();

  public:
	// C2 creature pose 255 is a real pose, so its repeat marker must fit
	// outside the byte range. Other animation formats retain marker 255.
	std::vector<uint16_t> animation;
	uint16_t animation_loop_marker = 255;
	virtual void setPose(unsigned int p) = 0;
	virtual void setFrameNo(unsigned int f) = 0;
	unsigned int getFrameNo() { return frameno; }
};
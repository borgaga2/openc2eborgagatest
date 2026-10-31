#pragma once
#include <cmath>
#include "Room.h"

// Native C2 tests each axis independently and uses the shorter horizontal
// distance around the world. This is only its range gate, not room delivery.
inline bool c2WithinHearingRange(float dx, float dy, float range, float world_width) {
	if (range < 0) return false;
	dx = std::fabs(dx);
	if (world_width > 0) {
		dx = std::fmod(dx, world_width);
		dx = std::fmin(dx, world_width - dx);
	}
	return dx <= range && std::fabs(dy) <= range;
}

// Native floor-side classification uses half-open segment ranges and a
// strict comparison: a point exactly on the floor is not above it.
inline bool c2AboveFloor(const Room& room, float x, float y) {
	const int local_x = static_cast<int>(x) - static_cast<int>(room.x_left);
	for (size_t i = 1; i < room.floorpoints.size(); ++i) {
		const auto [x0, h0] = room.floorpoints[i-1];
		const auto [x1, h1] = room.floorpoints[i];
		if (local_x < static_cast<int>(x0) || local_x >= static_cast<int>(x1)) continue;
		const float slope = float(int(h1)-int(h0)) / float(int(x1)-int(x0));
		const int height = int(h0) + int(slope * (local_x-int(x0)));
		return static_cast<int>(y) < static_cast<int>(room.y_left_floor) - height;
	}
	return false;
}

class Agent;
bool c2CanHear(Agent* listener, Agent* source);

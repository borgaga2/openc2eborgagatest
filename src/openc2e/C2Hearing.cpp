#include "C2HearingRange.h"
#include "Agent.h"
#include "World.h"
#include "Map.h"
#include "MetaRoom.h"
#include <algorithm>
#include <limits>

bool c2CanHear(Agent* listener, Agent* source) {
	if (!listener || !source || listener == source || !listener->part(0) || !source->part(0)) return false;
	const Point start(listener->x+listener->getWidth()/2.0f, listener->y+listener->getHeight()/2.0f);
	const Point target(source->x+source->getWidth()/2.0f, source->y+source->getHeight()/2.0f);
	auto* meta = world.map->metaRoomAt(start.x,start.y);
	if (!meta || meta != world.map->metaRoomAt(target.x,target.y)) return false;
	const double width = meta->width();
	double dx=target.x-start.x, dy=target.y-start.y;
	if (!c2WithinHearingRange(dx,dy,listener->range,width)) return false;
	if (dx >= width/2) dx-=width;
	else if (dx < -width/2) dx+=width;
	auto normalize = [&](double x) { return float(meta->x()+std::fmod(std::fmod(x-meta->x(),width)+width,width)); };
	auto room=meta->roomAt(start.x,start.y), destination=meta->roomAt(target.x,target.y);
	// Native permits a range-qualified route with an unresolved endpoint.
	if (!room || !destination) return true;
	Point entry=start;
	double shift=0, time=0;
	for (size_t hops=0;hops<meta->rooms.size()*4+8;++hops) {
		if (room == destination) return room->floorvalue >= listener->size ||
			c2AboveFloor(*room,entry.x,entry.y)==c2AboveFloor(*room,target.x,target.y);
		double exit=std::numeric_limits<double>::infinity(); int direction=-1;
		auto edge=[&](double t,int side) { if(t>time && t<exit) {exit=t;direction=side;} };
		if(dx>0) edge((room->x_right+shift-start.x)/dx,1);
		if(dx<0) edge((room->x_left+shift-start.x)/dx,0);
		if(dy>0) edge((room->y_left_floor-start.y)/dy,3);
		if(dy<0) edge((room->y_left_ceiling-start.y)/dy,2);
		if(direction<0 || exit>1) return false;
		const double step=0.05/std::max({std::fabs(dx),std::fabs(dy),1.0});
		const double next=std::min(1.0,exit+step);
		const double rawx=start.x+dx*next;
		Point sample(normalize(rawx),float(start.y+dy*next));
		auto next_room=meta->roomAt(sample.x,sample.y);
		if(!next_room || next_room==room) return false;
		bool pass=false;
		for(const auto& [other,door]:room->getDoors()) if(other==next_room) {pass=door.perm>=listener->size;break;}
		if(!pass || (direction==3 && room->floorvalue<listener->size)) return false;
		room=std::move(next_room);entry=sample;time=next;
		shift=std::round((rawx-sample.x)/width)*width;
	}
	return false;
}

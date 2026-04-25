#ifndef CACHES_H
#define CACHES_H

#include "common.h"
#include "collision_pair.h"
#include "tile_actor.h"
#include "trace_line_actor.h"
#include "trace_polygon_actor.h"
#include "interfaces.h"

// provides various caches for various collision routines to be used

struct caches
{
	static thread_local caches inst;

	std::vector<emu::collision_pair*> collision_pairs_ptr;
	std::vector<emu::collision_pair> collision_pairs;

	std::vector<std::unique_ptr<emu::tile_actor>> tile_actors;
	size_t tile_actors_count;

	std::vector<std::unique_ptr<emu::trace_line_actor>> trace_line_actors;
	size_t trace_line_actors_count;

	std::vector<std::unique_ptr<emu::trace_polygon_actor>> trace_polygon_actors;
	size_t trace_polygon_actors_count;

	std::vector<emu::actor*> query_results;
};

#endif

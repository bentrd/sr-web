#ifndef TRAJECTORY_PREDICTION_H
#define TRAJECTORY_PREDICTION_H

#include "event.h"
#include "level_preprocessing.h"
#include "../emulation/timespan.h"

namespace util
{
	// This struct contains a few important predictions of the player's
	// current trajectory, like when they will be able to perform the next grapple or
	// when they will collide next
	struct traj_predictions
	{
		struct grap_prediction
		{
			emu::timespan time = SIZE_MAX;
			emu::timespan con_time = SIZE_MAX;
		};

		grap_prediction grap_earliest;
		grap_prediction back_grap_earliest;
		grap_prediction grap_latest;
		grap_prediction back_grap_latest;

		event next_event;

		static traj_predictions get(const emu::state& state, const level_prep& prep, bool jump, size_t player);
	};
}

#endif

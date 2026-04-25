#include "trajectory_prediction.h"
#include "../emulation/player.h"
#include "../instance.h"

using namespace util;
using namespace emu;

traj_predictions traj_predictions::get(const state& state, const level_prep& prep, bool jump, size_t player_index)
{
	traj_predictions traj_pred;
	thread_local static emu::state state_cpy;
	state_cpy = state;

	if (jump)
		state_cpy.m_inputs[player_index][inp_jump] = true;

	emu::player& player = *state_cpy.get_contr<emu::player>(player_index);

	//bool prev_ignore_collision = false;

	get_event_helper helper{ player };

	for (size_t i = 0; i < 1500; i++)
	{
		int32_t dir = state_cpy.m_inputs[player_index][inp_right] ? 1 : -1;
		float cur_dist;

		if (player.d.can_grapple)
		{
			// forward grapple
			cur_dist = prep.get_grap_dist(dir, player);
			if (cur_dist != FLOAT_MAX)
			{
				if (traj_pred.grap_earliest.time == SIZE_MAX)
				{
					traj_pred.grap_earliest.time = state_cpy.m_time - state.m_time;
					traj_pred.grap_earliest.con_time = traj_pred.grap_earliest.time + timespan{ cur_dist / 2121.32f };
				}
				if (traj_pred.grap_earliest.time != SIZE_MAX)
				{
					traj_pred.grap_latest.time = state_cpy.m_time - state.m_time;
					traj_pred.grap_latest.con_time = traj_pred.grap_latest.time + timespan{ cur_dist / 2121.32f };
				}
			}

			// back grapple
			cur_dist = prep.get_grap_dist(-dir, player);
			if (cur_dist != FLOAT_MAX)
			{
				if (traj_pred.back_grap_earliest.time == SIZE_MAX)
				{
					traj_pred.back_grap_earliest.time = state_cpy.m_time - state.m_time;
					traj_pred.back_grap_earliest.con_time = traj_pred.back_grap_earliest.time + timespan{ cur_dist / 2121.32f };
				}
				if (traj_pred.back_grap_earliest.time != SIZE_MAX)
				{
					traj_pred.back_grap_latest.time = state_cpy.m_time - state.m_time;
					traj_pred.back_grap_latest.con_time = traj_pred.back_grap_latest.time + timespan{ cur_dist / 2121.32f };
				}
			}
		}

		
		//bool ignore_collision = true;
		//
		//float exp_l = 5.0f;
		//float exp_r = 5.0f;
		//float exp_t = 2.0f;
		//float exp_b = 2.0f;
		//if (!player->m_in_air && !player->m_swinging && !(player->m_unknown1 || player->m_unknown4 || player->m_unknown5 > 0.0f) && player->m_on_ground && (!player->m_using_gold_hook || false) && !player->m_hooked && !player->m_stunned)
		//	exp_b = std::max(exp_b, 2.0f + std::abs(player->m_delta_pos.y));
		//if (!player->m_in_air && !player->m_swinging && !(player->m_unknown1 || player->m_unknown4 || player->m_unknown5 > 0.0f) && player->m_on_ground && !player->m_unknown12)
		//	exp_b = std::max(exp_b, 2.0f + std::abs(player->m_delta_pos.x));
		//if (player->m_unknown2 || (!player->m_on_ground && (player->m_touching_wall || player->m_on_wall) && ((player->m_left_p && player->m_move_dir == -1) || (player->m_right_p && player->m_move_dir == 1))))
		//	ignore_collision = false;
		//
		//ignore_collision = ignore_collision && !util.is_colliding(state_cpy, exp_l, exp_r, exp_t, exp_b);
		//
		//state_cpy.update(delta, ignore_collision && prev_ignore_collision);
		//prev_ignore_collision = ignore_collision;

		state_cpy.update(delta);

		traj_pred.next_event = helper.get_event(player);
	}

	traj_pred.next_event.time = state_cpy.m_time - state.m_time;
	return traj_pred;
}
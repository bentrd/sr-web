#ifndef STATE_H
#define STATE_H

#include <vector>
#include <array>

#include "common.h"
#include "actor.h"
#include "collision_engine.h"
#include "tile_layer_base.h"
#include "input.h"
#include "level.h"
#include "editable_actor.h"

namespace emu
{
	struct player;
	struct grapple;

	struct state
	{
		int32_t m_next_actor_id = 0;
		collision_engine m_collision_engine;
		timespan m_time = 0;

		std::array<std::array<bool, input_count>, 4> m_inputs{};

		state();
		state(level& level);
		state(const state& right);
		state(state&& right) noexcept;

		state& operator=(const state& right);
		state& operator=(state&& right) noexcept;

		std::vector<std::unique_ptr<actor>>& actors();
		const std::vector<std::unique_ptr<actor>>& actors() const;

		template <std::derived_from<i_actor_controller> T> requires
			std::is_default_constructible_v<T>
		T* spawn(vector position, int32_t id = -1)
		{
			std::unique_ptr<T> t = std::make_unique<T>();
			T* t_ptr = t.get();

			actor_init_params params = t_ptr->get_actor_params();

			std::unique_ptr<actor> actor = std::make_unique<emu::actor>(this, id == -1 ? m_next_actor_id++ : id, position, params.size);
			
			actor->d.is_collidable = params.is_col;
			actor->d.automatic_collision_detection = params.auto_col_det;
			actor->d.should_predict_collision = params.should_pred_col;
#ifdef OPTIMIZE_COLLISION
			actor->m_has_update = params.has_update;
			actor->m_is_movable = params.is_movable;
#endif

			actor->set_controller(std::move(t));
			t_ptr->reset();

			m_collision_engine.add_actor(std::move(actor));

			return t_ptr;
		}

		template <std::derived_from<editable_actor> T> requires
			std::is_constructible_v<T, level_actor>
		T* spawn(const level_actor& def, int32_t id = -1)
		{
			std::unique_ptr<T> t = std::make_unique<T>(def);
			T* t_ptr = t.get();

			actor_init_params params = t_ptr->get_actor_params();

			std::unique_ptr<actor> actor = std::make_unique<emu::actor>(this, id == -1 ? m_next_actor_id++ : id, def.position, def.size);

			actor->d.is_collidable = params.is_col;
			actor->d.automatic_collision_detection = params.auto_col_det;
			actor->d.should_predict_collision = params.should_pred_col;
#ifdef OPTIMIZE_COLLISION
			actor->m_has_update = params.has_update;
			actor->m_is_movable = params.is_movable;
#endif

			actor->set_controller(std::move(t));
			t_ptr->reset();

			m_collision_engine.add_actor(std::move(actor));

			return t_ptr;
		}

		template <std::derived_from<i_actor_controller> T>
		T* get_contr(size_t index)
		{
			for (auto& actor : actors())
			{
				if (T* contr = dynamic_cast<T*>(actor->m_controller.get()))
				{
					if (index == 0)
						return contr;
					index--;
				}
			}
			return nullptr;
		}

		template <std::derived_from<i_actor_controller> T>
		const T* get_contr(size_t index) const
		{
			for (const auto& actor : actors())
			{
				if (const T* contr = dynamic_cast<const T*>(actor->m_controller.get()))
				{
					if (index == 0)
						return contr;
					index--;
				}
			}
			return nullptr;
		}

		template <std::derived_from<i_actor_controller> T>
		size_t count() const
		{
			size_t count = 0;
			for (const auto& actor : actors())
			{
				if (const T* contr = dynamic_cast<const T*>(actor->m_controller.get()))
					count++;
			}
			return count;
		}

		void update(timespan delta);
	};
}

#endif

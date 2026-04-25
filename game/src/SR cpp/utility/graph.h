#ifndef GRAPH_H
#define GRAPH_H

#include <vector>
#include <set>

namespace util
{
	template <typename V>
	struct path
	{
		std::vector<const V*> vertices;
		float dist = INFINITY;

		const V* source()
		{
			return vertices.front();
		}

		const V* target()
		{
			return vertices.back();
		}
	};

	template <typename V>
	struct graph
	{
		std::vector<V> vertices;

		std::vector<float> dist_mat;
		std::vector<std::vector<std::size_t>> neighbors;

		graph() = default;

		graph(const std::vector<V>& vertices) :
			vertices(vertices),
			dist_mat(vertices.size() * vertices.size(), INFINITY),
			neighbors(vertices.size()) {}

		graph(std::vector<V>&& vertices) :
			vertices(std::move(vertices)),
			dist_mat(this->vertices.size() * this->vertices.size(), INFINITY),
			neighbors(this->vertices.size()) {}

		void connect(std::size_t from, std::size_t to, float dist)
		{
			dist_mat[from + vertices.size() * to] = dist;
			neighbors[from].push_back(to);
		}

		void connect_bi(std::size_t v1, std::size_t v2, float dist)
		{
			connect(v1, v2, dist);
			connect(v2, v1, dist);
		}

		float dist(std::size_t from, std::size_t to) const
		{
			return dist_mat[from + vertices.size() * to];
		}

		std::size_t index(const V* vertex) const
		{
			return std::distance(vertices.begin(), vertex);
		}

		std::vector<path<V>> dijkstra(std::size_t src) const
		{
			struct vert_info
			{
				std::size_t pred = SIZE_MAX;
				float path_dist = INFINITY;
				bool finished = false;
			};

			static thread_local std::vector<vert_info> vert_infos;
			vert_infos.clear();
			vert_infos.resize(vertices.size());

			auto comp = [](std::size_t v1, std::size_t v2)
				{
					return vert_infos[v1].path_dist < vert_infos[v2].path_dist;
				};
			std::multiset<std::size_t, decltype(comp)> border(comp);

			vert_infos[src].path_dist = 0;
			border.insert(src);

			while (!border.empty())
			{
				std::size_t u = *border.begin();
				border.erase(border.begin());

				vert_infos[u].finished = true;

				if (vert_infos[u].path_dist == INFINITY)
					continue;

				for (std::size_t i = 0; i < neighbors[u].size(); i++)
				{
					std::size_t v = neighbors[u][i];
					if (vert_infos[v].finished || dist(u, v) == INFINITY)
						continue;
				
					float new_dist = vert_infos[u].path_dist + dist(u, v);
					if (new_dist < vert_infos[v].path_dist)
					{
						bool was_inf = vert_infos[v].path_dist == INFINITY;

						vert_infos[v].path_dist = new_dist;
						vert_infos[v].pred = u;

						if (was_inf)
							border.insert(v);
						else
						{
							// remove v from border without removing other equivalent elements
							auto [it1, it2] = border.equal_range(v);
							for (; it1 != it2; ++it1)
								if (*it1 == v)
								{
									border.erase(it1);
									break;
								}

							border.insert(v);
						}
					}
				}
			}

			std::vector<path<V>> paths(vertices.size());
			for (std::size_t i = 0; i < vertices.size(); i++)
			{
				paths[i].dist = vert_infos[i].path_dist;
				paths[i].vertices.push_back(&vertices[i]);
				std::size_t p = i;
				while ((p = vert_infos[p].pred) != SIZE_MAX)
				{
					paths[i].vertices.push_back(&vertices[p]);
				}
				std::reverse(paths[i].vertices.begin(), paths[i].vertices.end());
			}

			return paths;
		}

		std::vector<std::vector<path<V>>> dijkstra_all() const
		{
			std::vector<std::vector<path<V>>> paths(vertices.size());
			for (std::size_t i = 0; i < vertices.size(); i++)
			{
				paths[i] = dijkstra(i);
			}

			return paths;
		}
	};
}

#endif

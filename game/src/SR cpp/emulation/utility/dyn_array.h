#ifndef DYN_ARRAY_H
#define DYN_ARRAY_H

#include <memory>
#include <ranges>

namespace emu
{
	// smaller alternative to std::vector
	template <typename T>
	class dyn_array
	{
		std::unique_ptr<T[]> m_arr;
		T* m_end = nullptr;

	public:
		T* begin()
		{
			return m_arr.get();
		}

		T* end()
		{
			return m_end;
		}

		const T* begin() const
		{
			return m_arr.get();
		}

		const T* end() const
		{
			return m_end;
		}

		const T* cbegin() const
		{
			return begin();
		}

		const T* cend() const
		{
			return end();
		}

		std::size_t size() const
		{
			return static_cast<std::size_t>(std::distance(begin(), end()));
		}

		bool empty() const
		{
			return begin() == end();
		}

		dyn_array() = default;

		explicit dyn_array(size_t size) :
			m_arr{ std::make_unique_for_overwrite<T[]>(size) },
			m_end{ begin() + size } {}

		dyn_array(const dyn_array& right)
		{
			if (!right.empty())
			{
				m_arr = std::make_unique_for_overwrite<T[]>(right.size());
				m_end = std::ranges::copy(right, begin()).out;
			}
		}

		dyn_array(dyn_array&&) noexcept = default;

		dyn_array& operator=(const dyn_array& right)
		{
			if (right.empty())
			{
				m_arr.reset();
				m_end = nullptr;
				return *this;
			}

			std::size_t right_size = right.size();

			if (size() != right.size())
				m_arr = std::make_unique_for_overwrite<T[]>(right_size);
			
			m_end = std::ranges::copy(right, begin()).out;
			return *this;
		}

		dyn_array& operator=(dyn_array&&) noexcept = default;

		T& operator[](std::size_t i)
		{
			return m_arr[i];
		}

		const T& operator[](std::size_t i) const
		{
			return m_arr[i];
		}
	};
}

#endif
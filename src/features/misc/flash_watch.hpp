#pragma once

#include <atomic>
#include <chrono>

#include <simulation/flash_model.hpp>

namespace features::misc {

	class flash_watch_t
	{
	public:
		void tick( );

		[[nodiscard]] simulation::flash::state snapshot( ) const noexcept
		{
			return simulation::flash::state{
				m_remaining.load( std::memory_order_relaxed ),
				m_blindness.load( std::memory_order_relaxed ),
				m_blindness_age.load( std::memory_order_relaxed ) };
		}

	private:
		std::chrono::steady_clock::time_point m_last_tick{};
		std::atomic<float> m_remaining{};
		std::atomic<float> m_blindness{};
		std::atomic<float> m_blindness_age{};
		bool m_ticked{};
	};

	inline flash_watch_t& flash_watch( )
	{
		static flash_watch_t value{};
		return value;
	}

} // namespace features::misc

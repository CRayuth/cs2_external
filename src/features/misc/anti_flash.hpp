#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace features::misc {

	namespace detail {

		[[nodiscard]] inline bool plausible_flash_seconds( const float value )
		{
			return std::isfinite( value ) && value > 0.0f && value < 30.0f;
		}

		[[nodiscard]] inline bool flash_state_present( const float value )
		{
			return std::isfinite( value ) && value > 0.0f;
		}
	}

	class anti_flash_t
	{
	public:
		void tick( );

		[[nodiscard]] bool armed( ) const noexcept
		{
			return m_armed.load( std::memory_order_relaxed );
		}

		[[nodiscard]] std::uint64_t writes( ) const noexcept
		{
			return m_writes.load( std::memory_order_relaxed );
		}

	private:
		std::chrono::steady_clock::time_point m_next_write{};
		std::atomic<std::uint64_t> m_writes{};
		std::atomic<bool> m_armed{};
		bool m_reported{};
	};

	inline anti_flash_t& anti_flash( )
	{
		static anti_flash_t value{};
		return value;
	}

} // namespace features::misc

#pragma once

#include <algorithm>
#include <cmath>

namespace simulation::flash
{
	inline constexpr float k_max_seconds{ 29.99f };
	inline constexpr float k_active_seconds{ 0.05f };
	inline constexpr float k_blind_threshold{ 0.02f };
	inline constexpr float k_sample_hold_seconds{ 0.12f };

	struct sample
	{
		float blindness{};
		float remaining{};
	};

	struct state
	{
		float remaining{};
		float blindness{};
		float blindness_age{};
	};

	[[nodiscard]] inline bool plausible_seconds( const float value ) noexcept
	{
		return std::isfinite( value ) && value > 0.0f && value < 30.0f;
	}

	[[nodiscard]] inline bool has_countdown( const float remaining ) noexcept
	{
		return std::isfinite( remaining ) && remaining > k_active_seconds;
	}

	[[nodiscard]] inline float normalized_blindness( const float alpha, const float max_alpha ) noexcept
	{
		if ( !std::isfinite( alpha ) || alpha <= 0.0f )
			return 0.0f;
		if ( std::isfinite( max_alpha ) && max_alpha > 0.01f )
			return std::clamp( alpha / max_alpha, 0.0f, 1.0f );
		return std::clamp( alpha > 1.5f ? alpha / 255.0f : alpha, 0.0f, 1.0f );
	}

	[[nodiscard]] inline float remaining_seconds( const float bang_time, const float duration,
		const float game_time ) noexcept
	{
		if ( !std::isfinite( bang_time ) || bang_time <= 0.0f || !std::isfinite( game_time ) )
			return 0.0f;
		if ( !plausible_seconds( duration ) )
			return 0.0f;
		const auto elapsed = std::max( 0.0f, game_time - bang_time );
		const auto remaining = duration - elapsed;
		if ( !std::isfinite( remaining ) || remaining <= 0.0f )
			return 0.0f;
		return std::min( remaining, k_max_seconds );
	}

	[[nodiscard]] inline state advance( const state held, const float elapsed_seconds,
		const sample& observed ) noexcept
	{
		const auto step = std::isfinite( elapsed_seconds ) ? std::max( 0.0f, elapsed_seconds ) : 0.0f;
		const auto fresh_remaining = std::isfinite( observed.remaining )
			? std::max( 0.0f, observed.remaining ) : 0.0f;
		const auto fresh_blindness = std::isfinite( observed.blindness )
			? std::clamp( observed.blindness, 0.0f, 1.0f ) : 0.0f;

		state next{};
		next.remaining = std::min( k_max_seconds,
			std::max( std::max( 0.0f, held.remaining - step ), fresh_remaining ) );
		if ( fresh_blindness > 0.0f )
		{
			next.blindness = fresh_blindness;
		}
		else
		{
			next.blindness_age = held.blindness_age + step;
			if ( next.blindness_age <= k_sample_hold_seconds )
				next.blindness = held.blindness;
		}
		if ( next.remaining <= k_active_seconds && next.blindness <= k_blind_threshold )
			return {};
		return next;
	}

	[[nodiscard]] inline bool active( const state& value ) noexcept
	{
		return value.remaining > k_active_seconds || value.blindness > k_blind_threshold;
	}
}

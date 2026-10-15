#pragma once

#include <cstdint>
#include <string>

namespace features::visuals {

	enum class plant_verdict : std::uint8_t
	{
		hidden,
		unknown,
		will_finish,
		too_late
	};

	struct plant_alert_sample
	{
		bool planting{};
		float armed_end_time{};
		float current_time{};
		float round_start_time{};
		int round_length{};
		bool round_over{};
		bool has_site{};
		int site{};
	};

	struct plant_alert_state
	{
		bool visible{};
		plant_verdict verdict{ plant_verdict::hidden };
		float seconds_remaining{};
		bool has_site{};
		int site{};
	};

	[[nodiscard]] inline plant_alert_state evaluate_plant_alert( const plant_alert_sample& sample )
	{
		auto state = plant_alert_state{ };
		if ( !sample.planting )
			return state;

		const auto remaining = sample.armed_end_time - sample.current_time;
		if ( !( remaining > 0.0f ) )
			return state;

		state.visible = true;
		state.seconds_remaining = remaining;
		state.has_site = sample.has_site;
		state.site = sample.site;
		if ( sample.round_over )
			state.verdict = plant_verdict::too_late;
		else if ( sample.round_length <= 0 || !( sample.round_start_time > 0.0f ) )
			state.verdict = plant_verdict::unknown;
		else
		{
			const auto round_end = sample.round_start_time + static_cast<float>( sample.round_length );
			state.verdict = sample.armed_end_time < round_end
				? plant_verdict::will_finish
				: plant_verdict::too_late;
		}
		return state;
	}

	[[nodiscard]] inline std::string plant_seconds_text( const float seconds )
	{
		const auto clamped = seconds > 0.0f ? seconds : 0.0f;
		const auto whole = static_cast<int>( clamped );
		const auto tenth = static_cast<int>( clamped * 10.0f ) % 10;
		return std::to_string( whole ) + '.' + std::to_string( tenth );
	}

} // namespace features::visuals

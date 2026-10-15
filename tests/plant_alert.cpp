#include "test_support.hpp"

#include <features/visuals/plant_alert.hpp>

#include <cmath>
#include <limits>

namespace {

	using features::visuals::evaluate_plant_alert;
	using features::visuals::plant_alert_sample;
	using features::visuals::plant_alert_state;
	using features::visuals::plant_seconds_text;
	using features::visuals::plant_verdict;

	constexpr float k_nan = std::numeric_limits<float>::quiet_NaN( );
	constexpr float k_inf = std::numeric_limits<float>::infinity( );

	[[nodiscard]] plant_alert_sample live_sample( )
	{
		auto sample = plant_alert_sample{ };
		sample.planting = true;
		sample.armed_end_time = 100.0f;
		sample.current_time = 98.7f;
		sample.round_start_time = 30.0f;
		sample.round_length = 115;
		sample.round_over = false;
		sample.has_site = true;
		sample.site = 0;
		return sample;
	}

	[[nodiscard]] bool close( const float left, const float right )
	{
		return std::fabs( left - right ) < 0.001f;
	}

} // namespace

int main( )
{
	auto state = plant_alert_state{ };

	state = evaluate_plant_alert( plant_alert_sample{ } );
	VESTA_CHECK( !state.visible );
	VESTA_CHECK( state.verdict == plant_verdict::hidden );
	VESTA_CHECK( close( state.seconds_remaining, 0.0f ) );

	auto sample = live_sample( );
	sample.planting = false;
	VESTA_CHECK( !evaluate_plant_alert( sample ).visible );

	sample = live_sample( );
	sample.current_time = sample.armed_end_time;
	VESTA_CHECK( !evaluate_plant_alert( sample ).visible );

	sample = live_sample( );
	sample.current_time = 100.4f;
	VESTA_CHECK( !evaluate_plant_alert( sample ).visible );

	sample = live_sample( );
	sample.armed_end_time = k_nan;
	VESTA_CHECK( !evaluate_plant_alert( sample ).visible );

	sample = live_sample( );
	sample.current_time = k_nan;
	VESTA_CHECK( !evaluate_plant_alert( sample ).visible );

	sample = live_sample( );
	sample.armed_end_time = k_inf;
	state = evaluate_plant_alert( sample );
	VESTA_CHECK( state.visible );
	VESTA_CHECK( state.verdict == plant_verdict::too_late );

	sample = live_sample( );
	state = evaluate_plant_alert( sample );
	VESTA_CHECK( state.visible );
	VESTA_CHECK( state.verdict == plant_verdict::will_finish );
	VESTA_CHECK( close( state.seconds_remaining, 1.3f ) );
	VESTA_CHECK( state.has_site );
	VESTA_CHECK( state.site == 0 );

	sample = live_sample( );
	sample.armed_end_time = 150.0f;
	sample.current_time = 148.7f;
	VESTA_CHECK( evaluate_plant_alert( sample ).verdict == plant_verdict::too_late );

	sample = live_sample( );
	sample.armed_end_time = 145.0f;
	sample.current_time = 143.0f;
	VESTA_CHECK( evaluate_plant_alert( sample ).verdict == plant_verdict::too_late );

	sample = live_sample( );
	sample.armed_end_time = 144.99f;
	sample.current_time = 143.0f;
	VESTA_CHECK( evaluate_plant_alert( sample ).verdict == plant_verdict::will_finish );

	sample = live_sample( );
	sample.armed_end_time = 160.0f;
	sample.round_over = true;
	VESTA_CHECK( evaluate_plant_alert( sample ).verdict == plant_verdict::too_late );

	sample = live_sample( );
	sample.round_length = 0;
	VESTA_CHECK( evaluate_plant_alert( sample ).verdict == plant_verdict::unknown );

	sample = live_sample( );
	sample.round_length = -1;
	VESTA_CHECK( evaluate_plant_alert( sample ).verdict == plant_verdict::unknown );

	sample = live_sample( );
	sample.round_start_time = 0.0f;
	VESTA_CHECK( evaluate_plant_alert( sample ).verdict == plant_verdict::unknown );

	sample = live_sample( );
	sample.round_start_time = k_nan;
	state = evaluate_plant_alert( sample );
	VESTA_CHECK( state.visible );
	VESTA_CHECK( state.verdict == plant_verdict::unknown );

	sample = live_sample( );
	sample.has_site = false;
	sample.site = 1;
	state = evaluate_plant_alert( sample );
	VESTA_CHECK( state.visible );
	VESTA_CHECK( !state.has_site );

	VESTA_CHECK( plant_seconds_text( 1.34f ) == "1.3" );
	VESTA_CHECK( plant_seconds_text( 9.99f ) == "9.9" );
	VESTA_CHECK( plant_seconds_text( 12.75f ) == "12.7" );
	VESTA_CHECK( plant_seconds_text( 0.0f ) == "0.0" );
	VESTA_CHECK( plant_seconds_text( -5.0f ) == "0.0" );
	VESTA_CHECK( plant_seconds_text( k_nan ) == "0.0" );
	VESTA_CHECK( plant_seconds_text( 59.5f ) == "59.5" );
	VESTA_CHECK( plant_seconds_text( 0.09f ) == "0.0" );

	std::cout << "plant_alert: ok\n";
	return 0;
}

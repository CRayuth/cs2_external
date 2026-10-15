#include "test_support.hpp"

#include <simulation/flash_model.hpp>

#include <cstdio>
#include <limits>

namespace {

	using namespace simulation::flash;

	constexpr float k_nan = std::numeric_limits<float>::quiet_NaN( );
	constexpr float k_inf = std::numeric_limits<float>::infinity( );

	[[nodiscard]] bool near( const float left, const float right )
	{
		return left > right - 0.011f && left < right + 0.011f;
	}

} // namespace

int main( )
{
	VESTA_CHECK( normalized_blindness( 0.0f, 0.0f ) == 0.0f );
	VESTA_CHECK( near( normalized_blindness( 0.5f, 0.0f ), 0.5f ) );
	VESTA_CHECK( near( normalized_blindness( 128.0f, 0.0f ), 128.0f / 255.0f ) );
	VESTA_CHECK( near( normalized_blindness( 0.9f, 1.8f ), 0.5f ) );
	VESTA_CHECK( normalized_blindness( 3.0f, 1.0f ) == 1.0f );
	VESTA_CHECK( normalized_blindness( k_nan, 1.0f ) == 0.0f );
	VESTA_CHECK( normalized_blindness( 1.0f, k_inf ) == 1.0f );

	VESTA_CHECK( plausible_seconds( 1.0f ) );
	VESTA_CHECK( !plausible_seconds( 0.0f ) );
	VESTA_CHECK( !plausible_seconds( 30.0f ) );

	VESTA_CHECK( near( remaining_seconds( 10.0f, 5.0f, 11.0f ), 4.0f ) );
	VESTA_CHECK( remaining_seconds( 10.0f, 5.0f, 15.0f ) == 0.0f );
	VESTA_CHECK( remaining_seconds( 10.0f, 5.0f, 20.0f ) == 0.0f );
	VESTA_CHECK( near( remaining_seconds( 10.0f, 5.0f, 9.5f ), 5.0f ) );
	VESTA_CHECK( remaining_seconds( 0.0f, 5.0f, 11.0f ) == 0.0f );
	VESTA_CHECK( remaining_seconds( 10.0f, 0.0f, 11.0f ) == 0.0f );
	VESTA_CHECK( remaining_seconds( 10.0f, 40.0f, 11.0f ) == 0.0f );
	VESTA_CHECK( remaining_seconds( 10.0f, 5.0f, k_nan ) == 0.0f );

	const auto pinned = advance( state{ 2.0f, 0.0f, 0.0f }, 0.0f, sample{ } );
	VESTA_CHECK( near( pinned.remaining, 2.0f ) );
	VESTA_CHECK( active( pinned ) );

	const auto held = advance( state{ 3.0f, 0.8f, 0.0f }, 0.04f, sample{ } );
	VESTA_CHECK( near( held.remaining, 2.96f ) );
	VESTA_CHECK( near( held.blindness, 0.8f ) );
	VESTA_CHECK( active( held ) );

	const auto expired = advance( state{ 3.0f, 0.8f, 0.0f }, 0.3f, sample{ } );
	VESTA_CHECK( expired.blindness == 0.0f );
	VESTA_CHECK( near( expired.remaining, 2.7f ) );

	const auto renewed = advance( state{ 1.0f, 0.1f, 0.5f }, 0.5f, sample{ 0.95f, 4.0f } );
	VESTA_CHECK( near( renewed.remaining, 4.0f ) );
	VESTA_CHECK( near( renewed.blindness, 0.95f ) );
	VESTA_CHECK( renewed.blindness_age == 0.0f );

	const auto ceiling = advance( state{ }, 0.0f, sample{ 0.4f, 50.0f } );
	VESTA_CHECK( near( ceiling.remaining, k_max_seconds ) );

	const auto blind_only = advance( state{ }, 0.0f, sample{ 0.6f, 0.0f } );
	VESTA_CHECK( blind_only.remaining == 0.0f );
	VESTA_CHECK( active( blind_only ) );

	const auto cleared = advance( state{ 0.04f, 0.0f, 0.0f }, 0.01f, sample{ } );
	VESTA_CHECK( cleared.remaining == 0.0f );
	VESTA_CHECK( cleared.blindness == 0.0f );
	VESTA_CHECK( !active( cleared ) );

	const auto poisoned = advance( state{ 2.0f, 0.0f, 0.0f }, k_nan, sample{ k_nan, k_nan } );
	VESTA_CHECK( near( poisoned.remaining, 2.0f ) );
	VESTA_CHECK( poisoned.blindness == 0.0f );

	VESTA_CHECK( has_countdown( 0.1f ) );
	VESTA_CHECK( has_countdown( 2.75f ) );
	VESTA_CHECK( !has_countdown( 0.0f ) );
	VESTA_CHECK( !has_countdown( k_active_seconds ) );
	VESTA_CHECK( !has_countdown( -1.0f ) );
	VESTA_CHECK( !has_countdown( k_nan ) );
	VESTA_CHECK( !has_countdown( k_inf ) );

	std::puts( "PASS flash model counts down through pinned reads and clears when inactive" );
	return 0;
}

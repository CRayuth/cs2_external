#include "test_support.hpp"

#include <features/misc/anti_flash.hpp>

#include <cmath>
#include <limits>

namespace {

	using namespace features::misc::detail;

	constexpr float k_nan = std::numeric_limits<float>::quiet_NaN( );
	constexpr float k_inf = std::numeric_limits<float>::infinity( );

} // namespace

int main( )
{
	VESTA_CHECK( plausible_flash_seconds( 0.0001f ) );
	VESTA_CHECK( plausible_flash_seconds( 1.0f ) );
	VESTA_CHECK( plausible_flash_seconds( 7.5f ) );
	VESTA_CHECK( plausible_flash_seconds( 29.999f ) );

	VESTA_CHECK( !plausible_flash_seconds( 0.0f ) );
	VESTA_CHECK( !plausible_flash_seconds( -0.0f ) );
	VESTA_CHECK( !plausible_flash_seconds( 30.0f ) );
	VESTA_CHECK( !plausible_flash_seconds( 30.001f ) );
	VESTA_CHECK( !plausible_flash_seconds( 60.0f ) );

	VESTA_CHECK( !plausible_flash_seconds( -1.0f ) );
	VESTA_CHECK( !plausible_flash_seconds( -12345.0f ) );
	VESTA_CHECK( !plausible_flash_seconds( 1.0e6f ) );
	VESTA_CHECK( !plausible_flash_seconds( std::numeric_limits<float>::max( ) ) );
	VESTA_CHECK( plausible_flash_seconds( std::numeric_limits<float>::denorm_min( ) ) );
	VESTA_CHECK( !plausible_flash_seconds( k_nan ) );
	VESTA_CHECK( !plausible_flash_seconds( k_inf ) );
	VESTA_CHECK( !plausible_flash_seconds( -k_inf ) );

	for ( int step = 1; step <= 600; ++step )
	{
		const auto value = static_cast< float >( step ) * 0.1f;
		VESTA_CHECK( plausible_flash_seconds( value ) == ( value > 0.0f && value < 30.0f ) );
	}

	VESTA_CHECK( flash_state_present( 0.0001f ) );
	VESTA_CHECK( flash_state_present( 0.5f ) );
	VESTA_CHECK( flash_state_present( 1.0f ) );
	VESTA_CHECK( flash_state_present( 255.0f ) );

	VESTA_CHECK( !flash_state_present( 0.0f ) );
	VESTA_CHECK( !flash_state_present( -0.0f ) );
	VESTA_CHECK( !flash_state_present( -1.0f ) );
	VESTA_CHECK( !flash_state_present( -0.5f ) );
	VESTA_CHECK( !flash_state_present( k_nan ) );
	VESTA_CHECK( !flash_state_present( k_inf ) );
	VESTA_CHECK( !flash_state_present( -k_inf ) );
	VESTA_CHECK( flash_state_present( std::numeric_limits<float>::max( ) ) );

	for ( int step = -50; step <= 50; ++step )
	{
		const auto value = static_cast< float >( step ) * 0.02f;
		VESTA_CHECK( flash_state_present( value ) == ( value > 0.0f ) );
	}

	return 0;
}

#include <stdafx.hpp>
#include <app/context.hpp>
#include <core/state/services.hpp>
#include <features/misc/flash_watch.hpp>

namespace features::misc {
	namespace {
		[[nodiscard]] std::int32_t flash_bang_time_offset( )
		{
			static const auto offset = SCHEMA(
				"C_CSPlayerPawnBase", "m_flFlashBangTime"_id );
			return offset;
		}

		[[nodiscard]] std::int32_t flash_duration_offset( )
		{
			static const auto offset = [ ]( )
			{
				auto value = SCHEMA( "C_CSPlayerPawnBase", "m_flFlashDuration"_id );
				if ( value <= 0 )
					value = SCHEMA( "C_CSPlayerPawn", "m_flFlashDuration"_id );
				return value;
			}( );
			return offset;
		}

		[[nodiscard]] std::int32_t flash_alpha_offset( )
		{
			static const auto offset = SCHEMA(
				"C_CSPlayerPawnBase", "m_flFlashOverlayAlpha"_id );
			return offset;
		}

		[[nodiscard]] std::int32_t flash_max_alpha_offset( )
		{
			static const auto offset = SCHEMA(
				"C_CSPlayerPawnBase", "m_flFlashMaxAlpha"_id );
			return offset;
		}
	}

	void flash_watch_t::tick( )
	{
		const auto now = std::chrono::steady_clock::now( );
		const auto elapsed = this->m_ticked
			? std::chrono::duration<float>( now - this->m_last_tick ).count( ) : 0.0f;
		this->m_last_tick = now;
		this->m_ticked = true;

		simulation::flash::sample observed{};
		const auto& local = game::local_player( );
		const auto pawn = local.alive( ) ? local.pawn( ) : std::uintptr_t{};
		const auto bang_offset = flash_bang_time_offset( );
		const auto duration_offset = flash_duration_offset( );
		const auto alpha_offset = flash_alpha_offset( );
		const auto max_alpha_offset = flash_max_alpha_offset( );
		if ( pawn && bang_offset > 0 && duration_offset > 0 && alpha_offset > 0 )
		{
			auto& process = app::context( ).process;
			const auto bang = process.load<float>( pawn
				+ static_cast<std::uintptr_t>( bang_offset ), 0.0f );
			const auto duration = process.load<float>( pawn
				+ static_cast<std::uintptr_t>( duration_offset ), 0.0f );
			const auto alpha = process.load<float>( pawn
				+ static_cast<std::uintptr_t>( alpha_offset ), 0.0f );
			const auto max_alpha = max_alpha_offset > 0
				? process.load<float>( pawn + static_cast<std::uintptr_t>( max_alpha_offset ), 0.0f )
				: 0.0f;
			observed.blindness = simulation::flash::normalized_blindness( alpha, max_alpha );
			observed.remaining = simulation::flash::remaining_seconds(
				bang, duration, local.game_time( ) );
		}

		const simulation::flash::state held{
			this->m_remaining.load( std::memory_order_relaxed ),
			this->m_blindness.load( std::memory_order_relaxed ),
			this->m_blindness_age.load( std::memory_order_relaxed ) };
		const auto next = simulation::flash::advance( held, elapsed, observed );
		this->m_remaining.store( next.remaining, std::memory_order_relaxed );
		this->m_blindness.store( next.blindness, std::memory_order_relaxed );
		this->m_blindness_age.store( next.blindness_age, std::memory_order_relaxed );
	}

} // namespace features::misc

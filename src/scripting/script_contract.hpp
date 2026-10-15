#pragma once

#include <chrono>
#include <cstddef>

namespace scripting {

	inline constexpr std::size_t k_memory_limit = 64u * 1024u * 1024u;
	inline constexpr std::size_t k_draw_limit = 8192;
	inline constexpr std::size_t k_ui_limit = 512;
	inline constexpr std::chrono::milliseconds k_tick_period{ 16 };
	inline constexpr std::chrono::milliseconds k_reload_probe_period{ 250 };
	inline constexpr std::chrono::milliseconds k_callback_budget{ 8 };
	inline constexpr std::chrono::milliseconds k_config_publish_period{ 100 };
	inline constexpr std::size_t k_texture_max_edge = 4096;
	inline constexpr std::size_t k_texture_max_bytes = 16u * 1024u * 1024u;
	inline constexpr std::size_t k_texture_limit = 64;
	inline constexpr float k_panel_min_width = 80.0f;
	inline constexpr float k_panel_min_height = 36.0f;

	class tick_delta
	{
	public:
		using clock = std::chrono::steady_clock;

		explicit tick_delta( const clock::time_point begin ) : previous( begin )
		{
		}

		double advance( const clock::time_point now )
		{
			const auto seconds = std::chrono::duration<double>( now - previous ).count( );
			previous = now;
			return seconds > 0.0 ? seconds : 0.0;
		}

	private:
		clock::time_point previous{};
	};

} // namespace scripting

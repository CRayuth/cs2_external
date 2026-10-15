#include "test_support.hpp"

#include <scripting/script_contract.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

	struct lua_binding
	{
		std::string_view ns;
		std::string_view name;
	};

	inline constexpr lua_binding bindings[] = {
		{ "", "log" },
		{ "events", "on" }, { "events", "off" },
		{ "game", "snapshot" }, { "game", "radar_snapshot" }, { "game", "world_to_screen" },
		{ "game", "trace_ray" }, { "game", "bomb_damage" }, { "game", "predict_grenade" },
		{ "game", "radar_overview" }, { "game", "penetration_damage" },
		{ "config", "get" }, { "config", "set" }, { "config", "patch" },
		{ "config", "snapshot" }, { "config", "schema" },
		{ "storage", "get" }, { "storage", "set" }, { "storage", "remove" },
		{ "helpers", "start" }, { "helpers", "copy_text" },
		{ "assets", "weapon_font" },
		{ "ui", "text" }, { "ui", "button" }, { "ui", "toggle" }, { "ui", "slider" },
		{ "ui", "select" }, { "ui", "input" }, { "ui", "color" }, { "ui", "keybind" },
		{ "ui", "separator" }, { "ui", "get" }, { "ui", "consume" },
		{ "draw", "line" }, { "draw", "rect" }, { "draw", "filled_rect" },
		{ "draw", "circle" }, { "draw", "filled_circle" }, { "draw", "text" },
		{ "draw", "world_line" }, { "draw", "world_text" }, { "draw", "load_texture" },
		{ "draw", "image" }, { "draw", "panel" },
		{ "input", "is_down" }, { "input", "binding" }, { "input", "key" },
		{ "input", "tap" }, { "input", "mouse_move" }, { "input", "mouse_button" }
	};

	inline constexpr std::string_view api_fields[] = { "major", "minor", "script_dir", "data_dir" };

	inline constexpr std::string_view frame_events[] = {
		"frame", "game_connected", "game_disconnected", "map_changed",
		"player_joined", "player_left", "player_hurt", "player_killed",
		"weapon_changed", "projectile_created", "projectile_detonated",
		"projectile_removed", "bomb_planted", "bomb_defuse_started",
		"bomb_defuse_stopped", "bomb_defused", "bomb_resolved", "confirmed_hit"
	};

	inline constexpr std::string_view lifecycle_events[] = {
		"load", "tick", "frame", "unload", "config_changed"
	};

	inline constexpr std::string_view frame_fields[] = {
		"sequence", "timestamp_us", "connected", "menu_open", "input_ready", "map",
		"screen", "camera", "local_player", "players", "items", "projectiles",
		"spectators", "bomb"
	};

	inline constexpr std::string_view radar_omitted_fields[] = {
		"screen", "menu_open", "input_ready", "spectators", "bones", "hitboxes",
		"model_path", "matrix", "thrower_handle"
	};

	inline constexpr std::string_view snapshot_detail_fields[] = {
		"controller_index", "steam_id64", "rank", "wins", "rank_type",
		"last_fired_time", "emit_sound_time", "collision_center", "model_path",
		"legit_visible", "spotted_by_mask", "pin_pulled", "max_ammo", "bounces",
		"thrower_handle", "smoke_position", "smoke_voxel_size", "smoke_volume_received",
		"detonated", "smoke_active", "timer_length", "defuse_length", "tick_base",
		"flash_alpha", "weapon_type", "mode", "mins", "maxs", "radius"
	};

	inline constexpr std::string_view grenade_kinds[] = {
		"he", "flash", "smoke", "molotov", "incendiary", "decoy"
	};

	inline constexpr std::string_view input_actions[] = {
		"forward", "back", "left", "right", "walk", "duck", "jump", "attack", "attack2"
	};

	inline constexpr std::string_view config_trees[] = {
		"combat_global", "combat_overrides", "esp", "misc"
	};

	inline constexpr std::string_view contract_constants[] = {
		"k_memory_limit", "k_draw_limit", "k_ui_limit", "k_tick_period",
		"k_reload_probe_period", "k_callback_budget", "k_config_publish_period",
		"k_texture_max_edge", "k_texture_max_bytes", "k_texture_limit",
		"k_panel_min_width", "k_panel_min_height"
	};

	std::string read_file( const std::filesystem::path& path )
	{
		std::ifstream stream( path );
		VESTA_CHECK( stream.good( ) );
		return { std::istreambuf_iterator<char>( stream ), std::istreambuf_iterator<char>( ) };
	}

	bool contains( const std::string_view haystack, const std::string_view needle )
	{
		return haystack.find( needle ) != std::string_view::npos;
	}

	std::size_t occurrences( std::string_view text, const std::string_view needle )
	{
		std::size_t total{};
		for ( auto position = text.find( needle ); position != std::string_view::npos;
			position = text.find( needle, position + needle.size( ) ) ) ++total;
		return total;
	}

	std::string_view slice( const std::string_view text, const std::string_view begin_marker,
		const std::string_view end_marker )
	{
		const auto begin = text.find( begin_marker );
		VESTA_CHECK( begin != std::string_view::npos );
		const auto end = text.find( end_marker, begin + begin_marker.size( ) );
		VESTA_CHECK( end != std::string_view::npos );
		return text.substr( begin, end - begin );
	}

	std::string_view tail( const std::string_view text, const std::string_view marker )
	{
		const auto begin = text.find( marker );
		VESTA_CHECK( begin != std::string_view::npos );
		return text.substr( begin );
	}

	std::vector<std::string> string_literals( const std::string_view text )
	{
		std::vector<std::string> values{};
		for ( auto position = text.find( '"' ); position != std::string_view::npos; )
		{
			const auto end = text.find( '"', position + 1 );
			VESTA_CHECK( end != std::string_view::npos );
			values.emplace_back( text.substr( position + 1, end - position - 1 ) );
			position = text.find( '"', end + 1 );
		}
		return values;
	}

	std::vector<std::string> registered_names( const std::string_view api )
	{
		static constexpr std::string_view marker{ "set_function( lua, \"" };
		std::vector<std::string> names{};
		for ( auto position = api.find( marker ); position != std::string_view::npos;
			position = api.find( marker, position + marker.size( ) ) )
		{
			const auto begin = position + marker.size( );
			const auto end = api.find( '"', begin );
			VESTA_CHECK( end != std::string_view::npos );
			names.emplace_back( api.substr( begin, end - begin ) );
		}
		return names;
	}

	std::vector<std::pair<std::string, std::string>> qualified_calls( const std::string_view text )
	{
		static constexpr std::string_view prefix{ "vesta." };
		const auto is_name_character = [ ]( const char value )
		{
			return value == '_' || ( value >= 'a' && value <= 'z' ) || ( value >= '0' && value <= '9' );
		};
		std::vector<std::pair<std::string, std::string>> calls{};
		for ( auto position = text.find( prefix ); position != std::string_view::npos;
			position = text.find( prefix, position + prefix.size( ) ) )
		{
			auto cursor = position + prefix.size( );
			const auto namespace_begin = cursor;
			while ( cursor < text.size( ) && is_name_character( text[ cursor ] ) ) ++cursor;
			const auto namespace_end = cursor;
			if ( namespace_end == namespace_begin || cursor >= text.size( ) || text[ cursor ] != '.' ) continue;
			++cursor;
			const auto name_begin = cursor;
			while ( cursor < text.size( ) && is_name_character( text[ cursor ] ) ) ++cursor;
			if ( cursor == name_begin ) continue;
			calls.emplace_back( text.substr( namespace_begin, namespace_end - namespace_begin ),
				text.substr( name_begin, cursor - name_begin ) );
		}
		return calls;
	}

	bool is_binding( const std::string_view ns, const std::string_view name )
	{
		return std::ranges::any_of( bindings, [ & ]( const lua_binding& binding )
		{
			return binding.ns == ns && binding.name == name;
		} );
	}

	std::vector<std::string_view> split_lines( const std::string_view text )
	{
		std::vector<std::string_view> lines{};
		for ( std::size_t begin{}; begin <= text.size( ); )
		{
			const auto end = text.find( '\n', begin );
			const auto stop = end == std::string_view::npos ? text.size( ) : end;
			lines.push_back( text.substr( begin, stop - begin ) );
			if ( end == std::string_view::npos ) break;
			begin = end + 1;
		}
		return lines;
	}

	bool documented( const std::string_view text, const std::string_view prefix,
		const std::string_view name )
	{
		for ( const auto line : split_lines( text ) )
			if ( contains( line, prefix ) && contains( line, name ) ) return true;
		return false;
	}

} // namespace

int main( int argc, char** argv )
{
	if ( argc != 2 ) return 2;
	const std::filesystem::path root( argv[ 1 ] );
	const auto runtime = read_file( root / "src" / "scripting" / "runtime.cpp" );
	const auto contract = read_file( root / "src" / "scripting" / "script_contract.hpp" );
	const auto example = read_file( root / "lua" / "packages" / "web-radar" / "main.lua" );
	const auto config_file = read_file( root / "configs" / "legit.cfg" );
	const auto api = slice( runtime,
		"void runtime_t::implementation::script_instance::register_api(",
		"int runtime_t::implementation::script_instance::api_log(" );
	const auto frame_slice = slice( runtime, "void push_frame( lua_State* state,",
		"void push_radar_player( lua_State* state," );
	const auto radar_slice = slice( runtime, "void push_radar_frame( lua_State* state,",
		"} // namespace" );
	const auto event_slice = slice( runtime,
		"static constexpr std::array<std::string_view, 18> frame_events{", "};" );
	const auto create_slice = slice( runtime,
		"bool runtime_t::implementation::script_instance::create_state(",
		"bool runtime_t::implementation::script_instance::replace_state(" );
	const auto detail_slice = slice( runtime, "void push_weapon( lua_State* state,",
		"void push_radar_frame( lua_State* state," );

	std::vector<std::string> expected_names{};
	expected_names.reserve( std::ranges::size( bindings ) );
	for ( const auto& binding : bindings ) expected_names.push_back( std::string( binding.name ) );
	std::ranges::sort( expected_names );
	auto registered = registered_names( api );
	std::ranges::sort( registered );
	VESTA_CHECK( registered == expected_names );
	VESTA_CHECK( occurrences( runtime, "set_function( lua, \"" ) == expected_names.size( ) );
	VESTA_CHECK( occurrences( api, "lua_setfield( lua, -2, \"" ) >= 9 );
	VESTA_CHECK( contains( api, "lua_setglobal( lua, \"vesta\" )" ) );
	VESTA_CHECK( contains( create_slice, "register_api( );" ) );
	VESTA_CHECK( contains( radar_slice, "push_radar_player( state," ) );
	VESTA_CHECK( occurrences( runtime, "emit( \"load\" )" ) == 2 );
	VESTA_CHECK( contains( create_slice, "if ( !provisional ) emit( \"load\" )" ) );
	VESTA_CHECK( !contains( runtime, "if ( create_state( ) ) emit( \"load\" )" ) );
	VESTA_CHECK( !contains( runtime, "lua_pushnumber( target, 0.016 )" ) );
	VESTA_CHECK( contains( runtime, "tick_delta delta{ next_tick }" ) );
	VESTA_CHECK( contains( runtime, "delta.advance( now )" ) );

	for ( const auto field : frame_fields )
		VESTA_CHECK( contains( frame_slice, "lua_setfield( state, -2, \"" + std::string( field ) + "\" )" ) );
	for ( const auto field : snapshot_detail_fields )
		VESTA_CHECK( contains( detail_slice, "\"" + std::string( field ) + "\"" ) );
	for ( const auto field : radar_omitted_fields )
		VESTA_CHECK( !contains( radar_slice, "\"" + std::string( field ) + "\"" ) );
	VESTA_CHECK( occurrences( event_slice, "\"" ) == std::ranges::size( frame_events ) * 2 );
	for ( const auto event : frame_events )
		VESTA_CHECK( contains( event_slice, "\"" + std::string( event ) + "\"" ) );
	for ( const auto event : lifecycle_events )
		VESTA_CHECK( contains( runtime, "emit( \"" + std::string( event ) + "\"" ) );
	for ( const auto kind : grenade_kinds )
		VESTA_CHECK( contains( runtime, "name == \"" + std::string( kind ) + "\"" ) );
	for ( const auto action : input_actions )
		VESTA_CHECK( contains( runtime, "value == \"" + std::string( action ) + "\"" ) );
	for ( const auto tree : config_trees )
		VESTA_CHECK( contains( config_file, "\"" + std::string( tree ) + "\"" ) );

	for ( const auto name : contract_constants )
	{
		VESTA_CHECK( contains( contract, std::string( name ) ) );
		VESTA_CHECK( contains( runtime, std::string( name ) ) );
		VESTA_CHECK( !contains( runtime, "constexpr std::size_t " + std::string( name ) ) );
		VESTA_CHECK( !contains( runtime, "constexpr auto " + std::string( name ) ) );
	}

	for ( const auto* language : { "en", "ru" } )
	{
		const auto directory = std::string( language ) == "en"
			? std::filesystem::path( "lua" ) / "docs" / "en"
			: std::filesystem::path( "lua" ) / "docs";
		const auto reference = read_file( root / directory / "api-reference.md" );
		const auto events = read_file( root / directory / "events.md" );
		const auto snapshot = read_file( root / directory / "game-snapshot.md" );
		const auto lifecycle = read_file( root / directory / "runtime-and-lifecycle.md" );
		const auto drawing = read_file( root / directory / "ui-and-drawing.md" );
		const auto config = read_file( root / directory / "config.md" );
		const auto input = read_file( root / directory / "input.md" );

		for ( const auto& binding : bindings )
		{
			if ( binding.ns == "draw" )
				VESTA_CHECK( contains( slice( reference, "## Draw", "## Input" ), binding.name ) );
			else if ( binding.ns == "input" )
				VESTA_CHECK( contains( tail( reference, "## Input" ), binding.name ) );
			else if ( binding.ns.empty( ) )
				VESTA_CHECK( documented( reference, "vesta.", binding.name ) );
			else
				VESTA_CHECK( documented( reference,
					"vesta." + std::string( binding.ns ) + ".", binding.name ) );
		}

		for ( const auto event : frame_events )
			VESTA_CHECK( contains( events, "`" + std::string( event ) + "`" ) );
		for ( const auto event : lifecycle_events )
			VESTA_CHECK( contains( events, "`" + std::string( event ) + "`" ) );

		for ( const auto field : frame_fields )
			VESTA_CHECK( contains( snapshot, "`" + std::string( field ) + "`" ) );
		for ( const auto field : snapshot_detail_fields )
			VESTA_CHECK( contains( snapshot, "`" + std::string( field ) + "`" ) );
		for ( const auto kind : grenade_kinds )
			VESTA_CHECK( contains( snapshot, "`" + std::string( kind ) + "`" ) );
		for ( const auto action : input_actions )
			VESTA_CHECK( contains( input, "`" + std::string( action ) + "`" ) );
		for ( const auto tree : config_trees )
			VESTA_CHECK( contains( config, "`" + std::string( tree ) + "`" ) );
		for ( const auto* token : { "64 MiB", "8192", "512", "8 ms", "16" } )
			VESTA_CHECK( contains( lifecycle, token ) );
		for ( const auto* token : { "4096", "16 MiB", "64" } )
			VESTA_CHECK( contains( drawing, token ) );
		VESTA_CHECK( contains( lifecycle, "vesta.events.on(\"tick\", function(dt) end)" ) );
		VESTA_CHECK( contains( lifecycle, "`0`" ) );

		const auto used = qualified_calls( example );
		VESTA_CHECK( used.size( ) >= 8 );
		for ( const auto& [ ns, name ] : used )
		{
			if ( ns == "api" )
			{
				VESTA_CHECK( std::ranges::contains( api_fields, name ) );
				VESTA_CHECK( contains( api, "lua_setfield( lua, -2, \"" + name + "\" )" ) );
				VESTA_CHECK( documented( reference, "vesta.api.", name ) );
				continue;
			}
			VESTA_CHECK( is_binding( ns, name ) );
			if ( ns != "draw" && ns != "input" )
				VESTA_CHECK( documented( reference, "vesta." + ns + ".", name ) );
		}
	}

	VESTA_CHECK( scripting::k_memory_limit == 64u * 1024u * 1024u );
	VESTA_CHECK( scripting::k_draw_limit == 8192 );
	VESTA_CHECK( scripting::k_ui_limit == 512 );
	VESTA_CHECK( scripting::k_tick_period == std::chrono::milliseconds( 16 ) );
	VESTA_CHECK( scripting::k_reload_probe_period == std::chrono::milliseconds( 250 ) );
	VESTA_CHECK( scripting::k_callback_budget == std::chrono::milliseconds( 8 ) );
	VESTA_CHECK( scripting::k_config_publish_period == std::chrono::milliseconds( 100 ) );
	VESTA_CHECK( scripting::k_texture_max_edge == 4096 );
	VESTA_CHECK( scripting::k_texture_max_bytes == 16u * 1024u * 1024u );
	VESTA_CHECK( scripting::k_texture_limit == 64 );
	VESTA_CHECK( scripting::k_panel_min_width == 80.0f );
	VESTA_CHECK( scripting::k_panel_min_height == 36.0f );

	using clock = scripting::tick_delta::clock;
	const auto start = clock::now( );
	scripting::tick_delta delta{ start };
	VESTA_CHECK( delta.advance( start ) == 0.0 );
	VESTA_CHECK( std::abs( delta.advance( start + std::chrono::milliseconds( 25 ) ) - 0.025 ) < 1e-9 );
	VESTA_CHECK( delta.advance( start ) == 0.0 );
	VESTA_CHECK( std::abs( delta.advance( start + std::chrono::seconds( 1 ) ) - 1.0 ) < 1e-9 );
	VESTA_CHECK( contains( contract, "class tick_delta" ) );

	return 0;
}

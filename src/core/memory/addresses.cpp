#include <stdafx.hpp>
#include <core/memory/offsets_dump.hpp>

bool game::address_catalog::initialize( )
{
	dump_globals = 0;
	const auto& process = app::context().process;
	const auto client = app::context().modules.client;
	const auto image_size = process.module_image_size( client );
	if ( !client || !image_size )
		return false;

	const auto resolve = [&]( const std::string_view name,
		const std::string_view pattern, const std::string_view dump_key ) -> std::uintptr_t
	{
		const auto match = process.scan_code_signature( client, pattern );
		auto target = match ? process.decode_rip( match ) : 0;
		auto from_dump = false;
		if ( !target && !dump_key.empty( ) )
		{
			const auto rva = game::offsets( ).rva( dump_key );
			if ( rva )
			{
				target = client + rva;
				from_dump = true;
			}
		}
		if ( !target || target < client || target >= client + image_size )
		{
			app::context().diagnostics.warning(
				"[offsets] {} pattern is outdated{}.", name,
				from_dump ? " and the dump value falls outside this client.dll"
					: " and no usable dump value exists" );
			return 0;
		}
		std::byte probe{};
		if ( !process.copy( target, &probe, sizeof( probe ) ) )
		{
			app::context().diagnostics.warning(
				"[offsets] {} resolved to unreadable memory at client.dll+{:#x}.",
				name, target - client );
			return 0;
		}
		if ( from_dump )
		{
			++dump_globals;
			app::context().diagnostics.warning(
				"[offsets] {} taken from the community dump at client.dll+{:#x}; "
				"its signature needs re-syncing.", name, target - client );
		}
		return target;
	};

	csgo_input = resolve( "dwCSGOInput",
		"48 89 05 ? ? ? ? 0F 57 C0 0F 11 05", "dwCSGOInput" );
	entity_list = resolve( "dwEntityList",
		"48 89 0D ? ? ? ? E9 ? ? ? ? CC", "dwEntityList" );
	local_player_controller = resolve( "dwLocalPlayerController",
		"48 8B 05 ? ? ? ? 41 89 BE", "dwLocalPlayerController" );
	global_vars = resolve( "dwGlobalVars",
		"48 89 15 ? ? ? ? 48 89 42", "dwGlobalVars" );
	view_matrix = resolve( "dwViewMatrix",
		"48 8D 0D ? ? ? ? 48 C1 E0 06", "dwViewMatrix" );

	// Vesta-specific Panorama state has no upstream dumper entry and is optional.
	if ( const auto match = process.scan_code_signature( client,
		"48 89 05 ? ? ? ? E8 ? ? ? ? 48 85 DB" ) )
	{
		const auto target = process.decode_rip( match );
		if ( target >= client && target < client + image_size )
			auto_accept = target;
	}

	const auto complete = entity_list && local_player_controller
		&& global_vars && view_matrix;
	if ( complete )
	{
		if ( dump_globals )
			app::context().diagnostics.warning(
				"[offsets] {} global(s) resolved from the dump file, not from live "
				"patterns; every other feature is unaffected.", dump_globals );
		else
			app::context().diagnostics.success(
				"[offsets] live client.dll globals resolved from the current image." );
	}
	return complete;
}

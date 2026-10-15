#include "test_support.hpp"

#include <core/memory/offsets_dump.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {
	[[nodiscard]] bool write_file( const std::filesystem::path& path,
		const std::string_view text )
	{
		std::ofstream stream( path, std::ios::binary | std::ios::trunc );
		if ( !stream )
			return false;
		stream << text;
		return stream.good( );
	}
}

int main( )
{
	std::error_code error{};
	const auto directory = std::filesystem::current_path( ) / "offsets-dump-test";
	std::filesystem::create_directories( directory, error );
	VESTA_CHECK( !error );
	const auto file = directory / "offsets.json";
	auto& dump = game::offsets( );

	VESTA_CHECK( write_file( file, R"({
		"client.dll": {
			"dwCSGOInput": 39280992,
			"dwEntityList": 40982568,
			"dwGlobalVars": 35831448,
			"dwLocalPlayerController": 39026696,
			"dwViewMatrix": 39217424,
			"dwZero": 0,
			"dwTooLarge": 999999999999,
			"dwNotInteger": 1.5,
			"dwNegative": -5,
			"dwText": "x"
		},
		"engine2.dll": { "dwBuildNumber": 18123 }
	})" ) );

	VESTA_CHECK( dump.load_from( file ) );
	VESTA_CHECK( dump.entry_count( ) == 5 );
	VESTA_CHECK( dump.rva( "dwCSGOInput" ) == 39280992ull );
	VESTA_CHECK( dump.rva( "dwEntityList" ) == 40982568ull );
	VESTA_CHECK( dump.rva( "dwGlobalVars" ) == 35831448ull );
	VESTA_CHECK( dump.rva( "dwLocalPlayerController" ) == 39026696ull );
	VESTA_CHECK( dump.rva( "dwViewMatrix" ) == 39217424ull );
	VESTA_CHECK( dump.rva( "dwZero" ) == 0 );
	VESTA_CHECK( dump.rva( "dwTooLarge" ) == 0 );
	VESTA_CHECK( dump.rva( "dwNotInteger" ) == 0 );
	VESTA_CHECK( dump.rva( "dwNegative" ) == 0 );
	VESTA_CHECK( dump.rva( "dwText" ) == 0 );
	VESTA_CHECK( dump.rva( "dwAbsent" ) == 0 );
	VESTA_CHECK( dump.build_number( ) == 18123 );
	VESTA_CHECK( dump.status_text( ).find( "offsets.json" ) != std::string::npos );

	VESTA_CHECK( !dump.load_from( directory / "missing.json" ) );
	VESTA_CHECK( dump.entry_count( ) == 0 );
	VESTA_CHECK( dump.rva( "dwEntityList" ) == 0 );

	VESTA_CHECK( write_file( file, "{ not json" ) );
	VESTA_CHECK( !dump.load_from( file ) );
	VESTA_CHECK( dump.status_text( ).find( "JSON" ) != std::string::npos );
	VESTA_CHECK( dump.rva( "dwEntityList" ) == 0 );

	VESTA_CHECK( write_file( file, R"({"other.dll":{"dwEntityList":16}})" ) );
	VESTA_CHECK( !dump.load_from( file ) );

	VESTA_CHECK( write_file( file, R"({"client.dll":{}})" ) );
	VESTA_CHECK( !dump.load_from( file ) );

	VESTA_CHECK( write_file( file, R"({"client.dll":{"dwCustom":123}})" ) );
	VESTA_CHECK( dump.load_from( file ) );
	VESTA_CHECK( dump.entry_count( ) == 1 );
	VESTA_CHECK( dump.rva( "dwCustom" ) == 123 );
	VESTA_CHECK( dump.build_number( ) == -1 );

	std::filesystem::remove_all( directory, error );
	return EXIT_SUCCESS;
}

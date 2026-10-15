#include <stdafx.hpp>
#include <core/memory/offsets_dump.hpp>
#include <external/json.hpp>

#include <fstream>

namespace game {
	namespace {
		constexpr std::uint64_t k_maximum_rva = 0x0A000000ull;
		constexpr std::uintmax_t k_maximum_file_bytes = 1u << 20;

		[[nodiscard]] std::filesystem::path executable_directory( )
		{
			wchar_t path[MAX_PATH]{};
			const auto copied = ::GetModuleFileNameW(
				nullptr, path, static_cast<DWORD>( std::size( path ) ) );
			if ( !copied || copied >= std::size( path ) )
				return {};
			return std::filesystem::path( path ).parent_path( );
		}

		[[nodiscard]] std::filesystem::path locate_file( )
		{
			for ( const auto& directory : { executable_directory( ),
				platform::windows::runtime_storage::root( ) } )
			{
				if ( directory.empty( ) )
					continue;
				const auto candidate = directory / "offsets.json";
				std::error_code error{};
				if ( std::filesystem::is_regular_file( candidate, error ) )
					return candidate;
			}
			return {};
		}
	}

	bool offsets_catalog::load_from( const std::filesystem::path& file )
	{
		const auto& found = file;
		std::unordered_map<std::string, std::uintptr_t> entries{};
		std::int32_t build_number = -1;
		std::string error{};

		if ( found.empty( ) )
		{
			error = "no offsets.json beside the executable or in the runtime folder";
		}
		else
		{
			std::error_code size_error{};
			const auto bytes = std::filesystem::file_size( found, size_error );
			if ( size_error || bytes > k_maximum_file_bytes )
			{
				error = "offsets.json could not be read or is implausibly large";
			}
			else
			{
				std::ifstream stream( found, std::ios::binary );
				if ( !stream )
				{
					error = "offsets.json could not be opened";
				}
				else
				{
					const auto document = nlohmann::json::parse( stream, nullptr, false );
					if ( document.is_discarded( ) || !document.is_object( ) )
					{
						error = "offsets.json is not a valid JSON object";
					}
					else if ( const auto client = document.find( "client.dll" );
						client == document.end( ) || !client->is_object( ) )
					{
						error = "offsets.json has no client.dll section";
					}
					else
					{
						for ( auto entry = client->begin( ); entry != client->end( ); ++entry )
						{
							const auto& value = entry.value( );
							if ( !value.is_number_unsigned( ) )
								continue;
							const auto number = value.get<std::uint64_t>( );
							if ( number == 0 || number > k_maximum_rva )
								continue;
							entries.insert_or_assign( entry.key( ),
								static_cast<std::uintptr_t>( number ) );
						}
						if ( entries.empty( ) )
							error = "client.dll section holds no usable RVAs";
						else if ( const auto engine = document.find( "engine2.dll" );
							engine != document.end( ) && engine->is_object( ) )
						{
							if ( const auto build = engine->find( "dwBuildNumber" );
								build != engine->end( ) && build->is_number_integer( ) )
								build_number = build->get<std::int32_t>( );
						}
					}
				}
			}
		}

		if ( !error.empty( ) )
		{
			entries.clear( );
			build_number = -1;
		}

		const auto loaded = error.empty( );
		{
			const std::unique_lock lock( m_mutex );
			m_entries = std::move( entries );
			m_path = loaded ? found : std::filesystem::path{};
			m_error = error;
			m_build_number = build_number;
			m_loaded_at = std::chrono::system_clock::now( );
		}
		return loaded;
	}

	bool offsets_catalog::load( )
	{
		return load_from( locate_file( ) );
	}

	std::uintptr_t offsets_catalog::rva( const std::string_view key ) const
	{
		const std::shared_lock lock( m_mutex );
		const auto found = m_entries.find( std::string( key ) );
		return found == m_entries.end( ) ? 0 : found->second;
	}

	std::size_t offsets_catalog::entry_count( ) const
	{
		const std::shared_lock lock( m_mutex );
		return m_entries.size( );
	}

	std::int32_t offsets_catalog::build_number( ) const
	{
		const std::shared_lock lock( m_mutex );
		return m_build_number;
	}

	std::filesystem::path offsets_catalog::location( ) const
	{
		const std::shared_lock lock( m_mutex );
		return m_path;
	}

	std::string offsets_catalog::status_text( ) const
	{
		const std::shared_lock lock( m_mutex );
		if ( !m_error.empty( ) )
			return m_error;
		if ( m_entries.empty( ) )
			return "not loaded";

		const auto age = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now( ) - m_loaded_at ).count( );
		const auto filename = m_path.filename( ).string( );
		if ( age < 3600 )
			return std::format( "{}: {} globals, build {}, {} min ago",
				filename, m_entries.size( ), m_build_number, age / 60 );
		return std::format( "{}: {} globals, build {}, {} h ago",
			filename, m_entries.size( ), m_build_number, age / 3600 );
	}

} // namespace game

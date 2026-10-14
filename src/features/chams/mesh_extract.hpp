#pragma once

#include <features/chams/mesh.hpp>
#include <assets/vpk.hpp>

#include <string>

namespace features::chams
{

[[nodiscard]] skinned_mesh extract_mesh(assets::vpk_archive& vpk, const std::string& model_path);

} // namespace features::chams

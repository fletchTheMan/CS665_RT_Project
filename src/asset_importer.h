#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct AssetVertex {
	glm::vec3 position{0.0f, 0.0f, 0.0f};
	glm::vec3 normal{0.0f, 1.0f, 0.0f};
	glm::vec2 uv{0.0f, 0.0f};
	glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct AssetMesh {
	std::vector<AssetVertex> vertices;
	std::vector<uint32_t> indices;
};

// Loads all supported 3D assets found in the given directory (recursively)
// using Assimp and returns their meshes with node transforms baked into the
// vertex data. Files that cannot be imported are skipped with a warning.
std::vector<AssetMesh> load_assets_from_directory(const std::string& directory);

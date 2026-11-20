#include "asset_importer.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace {

glm::mat4 to_glm(const aiMatrix4x4& from) {
	// Assimp stores matrices in row-major order; GLM expects column-major.
	glm::mat4 to;
	to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
	to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
	to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
	to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
	return to;
}

bool is_supported_asset(const std::filesystem::path& path) {
	static const std::vector<std::string> extensions = {
		".fbx", ".obj", ".gltf", ".glb", ".dae", ".blend", ".3ds", ".stl",
		".ply", ".pmx", ".x", ".md5mesh", ".smd", ".lwo", ".lws", ".ms3d",
		".ase", ".ifc",
	};
	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

void process_mesh(const aiMesh* mesh, const glm::mat4& transform, AssetMesh& out) {
	const glm::mat3 normal_transform = glm::transpose(glm::inverse(glm::mat3(transform)));

	for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
		AssetVertex vertex{};

		const aiVector3D& p = mesh->mVertices[i];
		const glm::vec4 world_pos = transform * glm::vec4(p.x, p.y, p.z, 1.0f);
		vertex.position = glm::vec3(world_pos);

		if (mesh->HasNormals()) {
			const aiVector3D& n = mesh->mNormals[i];
			vertex.normal = glm::normalize(normal_transform * glm::vec3(n.x, n.y, n.z));
		} else {
			vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
		}

		if (mesh->HasTextureCoords(0)) {
			vertex.uv = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
		} else {
			vertex.uv = glm::vec2(0.0f, 0.0f);
		}

		if (mesh->HasVertexColors(0)) {
			const aiColor4D& c = mesh->mColors[0][i];
			vertex.color = glm::vec4(c.r, c.g, c.b, c.a);
		} else {
			vertex.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		out.vertices.push_back(vertex);
	}

	for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
		const aiFace& face = mesh->mFaces[i];
		// Triangulation is requested, but be defensive.
		if (face.mNumIndices == 3) {
			out.indices.push_back(face.mIndices[0]);
			out.indices.push_back(face.mIndices[1]);
			out.indices.push_back(face.mIndices[2]);
		}
	}
}

void process_node(const aiNode* node, const glm::mat4& parent_transform, const aiScene* scene,
                  std::vector<AssetMesh>& meshes) {
	const glm::mat4 transform = parent_transform * to_glm(node->mTransformation);

	for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
		const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		AssetMesh out_mesh{};
		process_mesh(mesh, transform, out_mesh);
		if (!out_mesh.vertices.empty() && !out_mesh.indices.empty()) {
			meshes.push_back(std::move(out_mesh));
		}
	}

	for (unsigned int i = 0; i < node->mNumChildren; ++i) {
		process_node(node->mChildren[i], transform, scene, meshes);
	}
}

void load_asset_file(const std::filesystem::path& path, std::vector<AssetMesh>& meshes) {
	Assimp::Importer importer;
	const unsigned int flags =
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ImproveCacheLocality |
		aiProcess_SortByPType |
		aiProcess_ValidateDataStructure;

	const aiScene* scene = importer.ReadFile(path.string(), flags);
	if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr) {
		std::cerr << "Assimp failed to load '" << path.string() << "': " << importer.GetErrorString() << std::endl;
		return;
	}

	const size_t before = meshes.size();
	process_node(scene->mRootNode, glm::mat4(1.0f), scene, meshes);
	std::cout << "Loaded asset '" << path.string() << "' (" << (meshes.size() - before) << " mesh(es))" << std::endl;
}

} // namespace

std::vector<AssetMesh> load_assets_from_directory(const std::string& directory) {
	std::vector<AssetMesh> meshes;

	std::error_code ec;
	if (!std::filesystem::exists(directory, ec) || ec) {
		std::cerr << "Asset directory '" << directory << "' does not exist; no assets loaded." << std::endl;
		return meshes;
	}

	std::vector<std::filesystem::path> files;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec)) {
		if (ec) {
			std::cerr << "Error while scanning asset directory '" << directory << "': " << ec.message() << std::endl;
			break;
		}
		if (entry.is_regular_file() && is_supported_asset(entry.path())) {
			files.push_back(entry.path());
		}
	}

	std::sort(files.begin(), files.end());
	for (const auto& path : files) {
		load_asset_file(path, meshes);
	}

	if (meshes.empty()) {
		std::cout << "No meshes loaded from '" << directory << "'." << std::endl;
	} else {
		std::cout << "Total meshes loaded from '" << directory << "': " << meshes.size() << std::endl;
	}

	return meshes;
}

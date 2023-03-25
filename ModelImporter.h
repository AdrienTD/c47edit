#pragma once

#include <optional>

struct Mesh;
struct Chunk;

namespace std::filesystem {
	class path;
}

std::optional<std::pair<Mesh, std::optional<Chunk>>> ImportWithAssimp(const std::filesystem::path& filename);
void ExportWithAssimp(const Mesh& gmesh, const std::filesystem::path& filename, Chunk* excChunk = nullptr);

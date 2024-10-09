#pragma once

#include <filesystem>
#include <optional>
#include <utility>

struct Mesh;
struct Chunk;

std::optional<std::pair<Mesh, std::optional<Chunk>>> ImportWithAssimp(const std::filesystem::path& filename);
void ExportWithAssimp(const Mesh& gmesh, const std::filesystem::path& filename, Chunk* excChunk = nullptr);

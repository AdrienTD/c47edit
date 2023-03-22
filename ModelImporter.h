#pragma once

struct Mesh;

namespace std::filesystem {
	class path;
}

Mesh ImportWithAssimp(const std::filesystem::path& filename);

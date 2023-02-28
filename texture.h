#pragma once

#include <cstdint>
#include <map>

struct Chunk;
namespace std::filesystem {
	class path;
}

struct TexInfo {
	uint32_t id;
	uint16_t height, width;
	uint32_t numMipmaps;
	uint32_t flags;
	uint32_t random;
	const char name[];
};

extern std::map<uint32_t, void*> texmap;

void GlifyTexture(Chunk* c);
void GlifyAllTextures();
void InvalidateTexture(uint32_t texid);
void AddTexture(const std::filesystem::path& filepath);
void ImportTexture(const std::filesystem::path& filepath, Chunk& chk, Chunk& dxtchk, int texid);
void ExportTexture(Chunk* texChunk, const std::filesystem::path& filepath);
std::pair<Chunk*, Chunk*> FindTextureChunk(uint32_t id);

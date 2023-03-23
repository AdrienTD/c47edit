#pragma once

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

struct Chunk;
struct Scene;
namespace std::filesystem {
	class path;
}

struct TexInfo {
	uint32_t id;
	uint16_t height, width;
	uint32_t numMipmaps;
	uint32_t flags;
	uint32_t random;
	const char name[1];
	TexInfo() = delete;
	const char* getName() const { return name; }
};

extern std::map<uint32_t, void*> texmap;

void GlifyTexture(Chunk* c);
void GlifyAllTextures();
void InvalidateTexture(uint32_t texid);
void UncacheAllTextures();
uint32_t AddTexture(Scene& scene, uint8_t* pixels, int width, int height, std::string_view name);
uint32_t AddTexture(Scene& scene, const std::filesystem::path& filepath);
uint32_t AddTexture(Scene& scene, const void* mem, size_t memSize, std::string_view name);
void ImportTexture(uint8_t* pixels, int width, int height, std::string_view name, Chunk& chk, Chunk& dxtchk, int texid);
void ImportTexture(const std::filesystem::path& filepath, Chunk& chk, Chunk& dxtchk, int texid);
void ImportTexture(const void* mem, size_t memSize, std::string_view name, Chunk& chk, Chunk& dxtchk, int texid);
void ExportTexture(Chunk* texChunk, const std::filesystem::path& filepath);
std::vector<uint8_t> ExportTextureToPNGInMemory(Chunk* texChunk);
std::pair<Chunk*, Chunk*> FindTextureChunk(Scene& scene, uint32_t id);
std::pair<Chunk*, Chunk*> FindTextureChunkByName(Scene& scene, std::string_view name);

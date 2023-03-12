#include "texture.h"

#include <filesystem>
#include <functional>
#include <map>
#include <sstream>

#include "global.h"
#include "chunk.h"
#include "gameobj.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <GL/GL.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include "squish.h"

std::map<uint32_t, void*> texmap;

void GlifyTexture(Chunk* c) {
	uint8_t* d = (uint8_t*)c->maindata.data();
	uint32_t texid = *(uint32_t*)d;
	uint32_t texh = *(uint16_t*)(d + 4);
	uint32_t texw = *(uint16_t*)(d + 6);
	uint32_t nmipmaps = *(uint16_t*)(d + 8);
	uint8_t* firstbmp = d + 20;
	while (*(firstbmp++));

	uint32_t pal[256];
	if (c->tag == 'PALN')
	{
		uint8_t* pnt = firstbmp;
		for (int m = 0; m < nmipmaps; m++)
			pnt += *(uint32_t*)pnt + 4;
		uint32_t npalentries = *(uint32_t*)pnt; pnt += 4;
		if (npalentries > 256) npalentries = 256;
		memcpy(pal, pnt, 4 * npalentries);
	}

	GLuint gltex;
	glGenTextures(1, &gltex);
	glBindTexture(GL_TEXTURE_2D, gltex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (nmipmaps > 1) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	texmap[texid] = (void*)gltex;

	uint8_t* bmp = firstbmp;
	for (int m = 0; m < nmipmaps; m++)
	{
		uint32_t mmsize = *(uint32_t*)bmp; bmp += 4;
		uint32_t mmWidth = std::max(texw >> m, 1u);
		uint32_t mmHeight = std::max(texh >> m, 1u);
		if (c->tag == 'PALN')
		{
			uint32_t* pix32 = new uint32_t[mmsize];
			for (uint32_t p = 0; p < mmsize; p++)
				pix32[p] = pal[bmp[p]];
			glTexImage2D(GL_TEXTURE_2D, m, 4, mmWidth, mmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix32);
			delete[] pix32;
		}
		else if (c->tag == 'RGBA')
			glTexImage2D(GL_TEXTURE_2D, m, 4, mmWidth, mmHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, bmp);
		else
			ferr("Unknown texture format in Pack(Repeat).PAL.");
		bmp += mmsize;
	}
}

void GlifyAllTextures()
{
	for (Chunk& c : g_scene.g_palPack.subchunks)
		GlifyTexture(&c);
	for (Chunk& c : g_scene.g_lgtPack.subchunks)
		GlifyTexture(&c);

	glBindTexture(GL_TEXTURE_2D, 0);
}

void InvalidateTexture(uint32_t texid)
{
	GLuint gltex = (GLuint)texmap.at(texid);
	glDeleteTextures(1, &gltex);
	GlifyTexture(FindTextureChunk(g_scene, texid).first);
}

void AddTexture(Scene& scene, const std::filesystem::path& filepath)
{
	Chunk* ptxi = scene.spkchk->findSubchunk('IXTP');
	assert(ptxi);
	uint32_t& numTextureIds = *(uint32_t*)ptxi->maindata.data();
	numTextureIds += 1;

	Chunk& chk = scene.g_palPack.subchunks.emplace_back();
	Chunk& dxtchk = scene.g_dxtPack.subchunks.emplace_back();
	ImportTexture(filepath, chk, dxtchk, numTextureIds);
}

void ImportTexture(const std::filesystem::path& filepath, Chunk& chk, Chunk& dxtchk, int texid)
{
	int width, height, channels;
	uint8_t* pixels = stbi_load(filepath.string().c_str(), &width, &height, &channels, 4);

	std::string name = filepath.stem().string();
	int numMipmaps = 1;
	int flags = 0x14;
	int random = 0x12345678;

	std::stringbuf chkdata, dxtdata;
	chkdata.sputn((char*)&texid, 4);
	chkdata.sputn((char*)&height, 2);
	chkdata.sputn((char*)&width, 2);
	chkdata.sputn((char*)&numMipmaps, 4);
	chkdata.sputn((char*)&flags, 4);
	chkdata.sputn((char*)&random, 4);
	chkdata.sputn(name.c_str(), name.size() + 1);
	std::string str = chkdata.str();
	dxtdata.sputn(str.data(), str.size());

	int size = width * height * 4;
	chkdata.sputn((char*)&size, 4);
	chkdata.sputn((char*)pixels, size);

	// 2 COPIES >_<
	chk.tag = 'RGBA';
	str = chkdata.str();
	chk.maindata.resize(str.size());
	memcpy(chk.maindata.data(), str.data(), str.size());

	// DXT
	size = squish::GetStorageRequirements(width, height, squish::kDxt1);
	auto comp = std::make_unique<uint8_t[]>(size);
	squish::CompressImage(pixels, width, height, comp.get(), squish::kDxt1);
	dxtdata.sputn((char*)&size, 4);
	dxtdata.sputn((char*)comp.get(), size);

	// 2 COPIES >_<
	dxtchk.tag = 'DXT1';
	str = dxtdata.str();
	dxtchk.maindata.resize(str.size());
	memcpy(dxtchk.maindata.data(), str.data(), str.size());

	stbi_image_free(pixels);
}

DynArray<uint32_t> ConvertTextureToRGBA8(Chunk* texChunk) {
	const TexInfo* ti = (const TexInfo*)texChunk->maindata.data();
	uint8_t* firstbmp = texChunk->maindata.data() + 20;
	while (*(firstbmp++)); // skip name

	if (texChunk->tag == 'PALN')
	{
		uint32_t pal[256];
		uint8_t* pnt = firstbmp;
		for (int m = 0; m < ti->numMipmaps; m++)
			pnt += *(uint32_t*)pnt + 4;
		uint32_t npalentries = *(uint32_t*)pnt; pnt += 4;
		if (npalentries > 256) npalentries = 256;
		memcpy(pal, pnt, 4 * npalentries);

		pnt = firstbmp;
		uint32_t mmsize = *(uint32_t*)pnt; pnt += 4;
		auto pix32 = DynArray<uint32_t>(mmsize);
		for (uint32_t p = 0; p < mmsize; p++)
			pix32[p] = pal[pnt[p]];
		return pix32;
	}
	else if (texChunk->tag == 'RGBA') {
		uint8_t* pnt = firstbmp;
		uint32_t mmsize = *(uint32_t*)pnt; pnt += 4;
		auto pix32 = DynArray<uint32_t>(mmsize/4);
		memcpy(pix32.data(), pnt, mmsize);
		return pix32;
	}
	return {};
}

void ExportTexture(Chunk* texChunk, const std::filesystem::path& filepath)
{
	const TexInfo* ti = (const TexInfo*)texChunk->maindata.data();
	auto rgba = ConvertTextureToRGBA8(texChunk);
	assert(rgba.size() > 0);
	stbi_write_png(filepath.string().c_str(), ti->width, ti->height, 4, rgba.data(), 0);
}

std::pair<Chunk*, Chunk*> FindTextureChunk(Scene& scene, uint32_t id)
{
	for (Chunk& chk : scene.g_palPack.subchunks) {
		uint32_t chkid = *(uint32_t*)chk.maindata.data();
		if (chkid == id) {
			int nth = &chk - scene.g_palPack.subchunks.data();
			assert(*(uint32_t*)scene.g_dxtPack.subchunks[nth].maindata.data() == id);
			return { &chk, &scene.g_dxtPack.subchunks[nth] };
		}
	}
	for (Chunk& chk : scene.g_lgtPack.subchunks) {
		uint32_t chkid = *(uint32_t*)chk.maindata.data();
		if (chkid == id) {
			return { &chk, nullptr };
		}
	}
	return { nullptr, nullptr };
}
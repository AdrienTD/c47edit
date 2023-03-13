// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include <cassert>

#include "video.h"
#include "global.h"
#include "texture.h"
#include "window.h"
#include "gameobj.h"
#include "chunk.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <GL/glew.h>
#include <GL/wglew.h>

#define glprocvalid(x) (! ( ((uintptr_t)x==-1) || (((uintptr_t)x >= 0) && ((uintptr_t)x <= 3)) ) )
typedef BOOL(APIENTRY *gli_wglSwapIntervalEXT)(int n);

HDC whdc; HGLRC glrc;
int drawframes = 0;
extern HWND hWindow;
bool rendertextures = false;
bool renderColorTextures = true, renderLightmaps = true;
bool enableAlphaTest = true;

void InitVideo()
{
	whdc = GetDC(hWindow);

	// Set the pixel format
	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR)); // Be sure that pfd is filled with 0.
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 24;
	pfd.cDepthBits = 32;
	pfd.dwLayerMask = PFD_MAIN_PLANE;
	int i = ChoosePixelFormat(whdc, &pfd);
	SetPixelFormat(whdc, i, &pfd);

	glrc = wglCreateContext(whdc);
	wglMakeCurrent(whdc, glrc);
	glewInit();

	// Set VSYNC
	if (WGLEW_EXT_swap_control) {
		if (!wglSwapIntervalEXT(1))
			printf("wglSwapIntervalEXT returned FALSE.\n");
	}
	else {
		printf("wglSwapIntervalEXT unsupported.\n");
	}
}

void BeginDrawing()
{
	glViewport(0, 0, screen_width, screen_height);
}

void EndDrawing()
{
	SwapBuffers(whdc); drawframes++;
}

// Prepared+Optimized Mesh for rendering
struct ProMesh {
	using IndexType = uint16_t;
	struct Part {
		std::vector<Vector3> vertices;
		std::vector<std::pair<float, float>> texcoords;
		std::vector<std::pair<float, float>> lightmapCoords;
		std::vector<uint32_t> colors;
		std::vector<IndexType> indices;
	};
	std::map<std::tuple<uint16_t, uint16_t, uint16_t>, Part> parts;

	inline static std::map<Mesh*, ProMesh> g_proMeshes;

	// Get a prepared mesh from the cache, make one if not already done
	static ProMesh* getProMesh(Mesh* mesh) {
		// If ProMesh found in the cache, return it
		auto it = g_proMeshes.find(mesh);
		if (it != g_proMeshes.end())
			return &it->second;

		// Else make one and return it:

		static const float defUvs[8] = { 0,0, 0,1, 1,1, 1,0 };
		static const uint32_t defColors[4] = { 0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFF000000 };
		static const int uvit[4] = { 0,1,2,3 };
		static const int lgtit[4] = { 0,1,3,2 };

		ProMesh pro;
		const float *verts = mesh->vertices.data();
		size_t numQuads = mesh->getNumQuads();
		size_t numTris = mesh->getNumTris();
		const uint16_t *ftxFace = (uint16_t*)mesh->ftxFaces.data();
		bool hasFtx = !mesh->ftxFaces.empty();

		float *uvCoords = (float*)defUvs;
		float *lgtCoords = (float*)defUvs;
		if (hasFtx) {
			uvCoords = (float*)mesh->textureCoords.data();
			lgtCoords = (float*)mesh->lightCoords.data();
		}

		assert(g_scene.g_lgtPack.subchunks[0].tag == 'RGBA');
		uint8_t* colorMapData = g_scene.g_lgtPack.subchunks[0].maindata.data();
		assert(*(uint16_t*)(colorMapData + 6) == 2); // the width of color map must be 2
		colorMapData += 0x14; // skip texture header until name
		while (*colorMapData++); // skip texture name
		colorMapData += 4; // skip mipmap size
		uint32_t* colorMap = (uint32_t*)colorMapData;

		auto nextFace = [&](int shape, ProMesh::IndexType* indices) {
			bool isTextured = hasFtx && (ftxFace[0] & 0x20);
			bool isLit = hasFtx && (ftxFace[0] & 0x80);
			uint16_t texid = isTextured ? ftxFace[2] : 0xFFFF;
			uint16_t lgtid = isLit ? ftxFace[3] : 0xFFFF;
			auto& part = pro.parts[std::make_tuple(ftxFace[0] & 0x0202, texid, lgtid)];
			IndexType prostart = (IndexType)part.vertices.size();
			for (int j = 0; j < shape; j++) {
				const float* uu = (isTextured ? uvCoords : defUvs) + uvit[j] * 2;
				part.texcoords.push_back({ uu[0], uu[1] });
				const float* lu = (isLit ? lgtCoords : defUvs) + uvit[j] * 2;
				float lmOffsetU = 0.0f, lmOffsetV = 0.0f;
				if (lgtid != 0xFFFF) {
					const TexInfo* lgtInfo = (const TexInfo*)FindTextureChunk(g_scene, lgtid).first->maindata.data();
					lmOffsetU = 0.5f / lgtInfo->width;
					lmOffsetV = 0.5f / lgtInfo->height;
				}
				part.lightmapCoords.push_back({ lu[0] + lmOffsetU, lu[1] + lmOffsetV });
				uint32_t color = (isLit && ftxFace[3] == 0xFFFF) ? colorMap[4 * (ftxFace[5] - 1) + lgtit[j]] : 0xFFFFFFFF;
				part.colors.push_back(color);
				const float* v = verts + indices[j] * 3 / 2;
				part.vertices.push_back({ v[0], v[1], v[2] });
			}
			for (int s = 2; s < shape; ++s)
				for (int j : {0, s - 1, s})
					part.indices.push_back((IndexType)(prostart + j));
			ftxFace += 6;
			if (isTextured) uvCoords += 8; // for triangles, 4th UV is ignored.
			if (isLit) lgtCoords += 8;
		};

		for (size_t i = 0; i < numTris; i++) {
			nextFace(3, mesh->triindices.data() + 3 * i);
		}
		for (size_t i = 0; i < numQuads; i++) {
			nextFace(4, mesh->quadindices.data() + 4 * i);
		}

		g_proMeshes[mesh] = std::move(pro);
		return &g_proMeshes[mesh];
	}
};

void DrawMesh(Mesh* mesh)
{
	if (!rendertextures)
	{
		glVertexPointer(3, GL_FLOAT, 6, mesh->vertices.data());
		glDrawElements(GL_QUADS, mesh->quadindices.size(), GL_UNSIGNED_SHORT, mesh->quadindices.data());
		glDrawElements(GL_TRIANGLES, mesh->triindices.size(), GL_UNSIGNED_SHORT, mesh->triindices.data());
	}
	else
	{
		ProMesh* pro = ProMesh::getProMesh(mesh);
		for (auto& [mat,part] : pro->parts) {
			auto& [flags, texid, lgtid] = mat;
			if (texid == 0xFFFF)
				continue;
			GLuint gltex = 0, gllgt = 0;
			if (renderColorTextures)
				if (auto t = texmap.find(texid); t != texmap.end())
					gltex = (GLuint)(uintptr_t)t->second;
			if (renderLightmaps)
				if (auto t = texmap.find(lgtid); t != texmap.end())
					gllgt = (GLuint)(uintptr_t)t->second;
			glActiveTextureARB(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, gltex);
			glActiveTextureARB(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, gllgt);
			if (enableAlphaTest && (flags & 0x0200)) {
				glEnable(GL_ALPHA_TEST);
				glAlphaFunc(GL_GEQUAL, 0.1f);
			}
			else {
				glDisable(GL_ALPHA_TEST);
			}
			//if (flags & 0x0002) {
			//	glEnable(GL_BLEND);
			//	glBlendFunc(GL_ONE, GL_ONE);
			//}
			//else {
			//	glDisable(GL_BLEND);
			//}
			glVertexPointer(3, GL_FLOAT, 12, part.vertices.data());
			if (renderLightmaps)
				glColorPointer(4, GL_UNSIGNED_BYTE, 4, part.colors.data());
			glClientActiveTextureARB(GL_TEXTURE0);
			glTexCoordPointer(2, GL_FLOAT, 8, part.texcoords.data());
			glClientActiveTextureARB(GL_TEXTURE1);
			glTexCoordPointer(2, GL_FLOAT, 8, part.lightmapCoords.data());
			glDrawElements(GL_TRIANGLES, part.indices.size(), GL_UNSIGNED_SHORT, part.indices.data());
		}
	}
}

void InvalidateMesh(Mesh* mesh)
{
	ProMesh::g_proMeshes.erase(mesh);
}

void BeginMeshDraw()
{
	if (!rendertextures) {
		glEnableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisable(GL_TEXTURE_2D);
	}
	else {
		if (!GLEW_ARB_multitexture)
			ferr("Your OpenGL driver doesn't support multitextures. Big oof.");
		glEnableClientState(GL_VERTEX_ARRAY);
		if (renderLightmaps)
			glEnableClientState(GL_COLOR_ARRAY);
		else
			glDisableClientState(GL_COLOR_ARRAY);
		glActiveTextureARB(GL_TEXTURE0);
		glClientActiveTextureARB(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glActiveTextureARB(GL_TEXTURE1);
		glClientActiveTextureARB(GL_TEXTURE1);
		glEnable(GL_TEXTURE_2D);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	}
	glColor4f(1, 1, 1, 1);
}

void EndMeshDraw()
{
	if (rendertextures) {
		glActiveTextureARB(GL_TEXTURE1);
		glClientActiveTextureARB(GL_TEXTURE1);
		glDisable(GL_TEXTURE_2D);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glActiveTextureARB(GL_TEXTURE0);
		glClientActiveTextureARB(GL_TEXTURE0);

		glDisable(GL_ALPHA_TEST);
		glDisable(GL_BLEND);
	}
}

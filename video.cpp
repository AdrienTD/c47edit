// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "video.h"
#include "global.h"
#include "texture.h"
#include "window.h"
#include "gameobj.h"
#include "chunk.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <gl/GL.h>

#define glprocvalid(x) (! ( ((uintptr_t)x==-1) || (((uintptr_t)x >= 0) && ((uintptr_t)x <= 3)) ) )
typedef BOOL(APIENTRY *gli_wglSwapIntervalEXT)(int n);

HDC whdc; HGLRC glrc;
int drawframes = 0;
extern HWND hWindow;
bool rendertextures = false;

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

	// Set VSYNC if enabled
	//if (VSYNCenabled)
	if(1)
	{
		gli_wglSwapIntervalEXT wglSwapIntervalEXT = (gli_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
		if (glprocvalid(wglSwapIntervalEXT))
		{
			if (!wglSwapIntervalEXT(1))
				printf("wglSwapIntervalEXT returned FALSE.\n");
		}
		else printf("wglSwapIntervalEXT unsupported.\n");
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
		std::vector<IndexType> indices;
	};
	std::map<uint16_t, Part> parts;

	inline static std::map<Mesh*, ProMesh> g_proMeshes;

	// Get a prepared mesh from the cache, make one if not already done
	static ProMesh* getProMesh(Mesh* mesh) {
		// If ProMesh found in the cache, return it
		auto it = g_proMeshes.find(mesh);
		if (it != g_proMeshes.end())
			return &it->second;

		// Else make one and return it:

		static const float defUvs[8] = { 0,0, 0,1, 1,1, 1,0 };
		static const uint32_t defColors[4] = { 0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFF0000FF };
		static const int uvit[4] = { 0,1,2,3 };

		ProMesh pro;
		uint16_t *faces = (uint16_t*)pfac->maindata.data();
		float *verts = (float*)pver->maindata.data() + mesh->vertstart;
		uint8_t *ftxpnt = (uint8_t*)pftx->maindata.data() + mesh->ftxo - 1;
		uint16_t *ftxFace = (uint16_t*)(ftxpnt + 12);
		bool hasFtx = mesh->ftxo && !(mesh->ftxo & 0x80000000);

		float *uvCoords = (float*)defUvs;
		if (hasFtx)
			uvCoords = (float*)puvc->maindata.data() + *(uint32_t*)(ftxpnt);

		for (uint32_t i = 0; i < mesh->numtris; i++)
		{
			uint16_t texid = hasFtx ? ftxFace[2] : 65535;
			auto& part = pro.parts[texid];
			IndexType prostart = (IndexType)part.vertices.size();
			for (int j = 0; j < 3; j++) {
				float* uu = uvCoords + uvit[j] * 2;
				part.texcoords.push_back({ uu[0], uu[1] });
				float *v = verts + faces[mesh->tristart + i * 3 + j] * 3 / 2;
				part.vertices.push_back({ v[0], v[1], v[2] });
			}
			for (int j : {0, 1, 2})
				part.indices.push_back((IndexType)(prostart + j));
			ftxFace += 6;
			uvCoords += 8; // Ignore 4th UV.
		}
		for (uint32_t i = 0; i < mesh->numquads; i++)
		{
			uint16_t texid = hasFtx ? ftxFace[2] : 65535;
			auto& part = pro.parts[texid];
			IndexType prostart = (IndexType)part.vertices.size();
			for (int j = 0; j < 4; j++) {
				float* uu = uvCoords + uvit[j] * 2;
				part.texcoords.push_back({ uu[0], uu[1] });
				float *v = verts + faces[mesh->quadstart + i * 4 + j] * 3 / 2;
				part.vertices.push_back({ v[0], v[1], v[2] });
			}
			for (int j : {0, 1, 3, 3, 1, 2 })
				part.indices.push_back((IndexType)(prostart + j));
			ftxFace += 6;
			uvCoords += 8;
		}

		g_proMeshes[mesh] = std::move(pro);
		return &g_proMeshes[mesh];
	}
};

void DrawMesh(Mesh* mesh)
{
	if (!rendertextures)
	{
		glVertexPointer(3, GL_FLOAT, 6, (float*)pver->maindata.data() + mesh->vertstart);
		glDrawElements(GL_QUADS, mesh->numquads * 4, GL_UNSIGNED_SHORT, (uint16_t*)pfac->maindata.data() + mesh->quadstart);
		glDrawElements(GL_TRIANGLES, mesh->numtris * 3, GL_UNSIGNED_SHORT, (uint16_t*)pfac->maindata.data() + mesh->tristart);
	}
	else
	{
		ProMesh* pro = ProMesh::getProMesh(mesh);
		for (auto& [texid,part] : pro->parts) {
			if (texid == 65535)
				continue;
			auto t = texmap.find(texid);
			if (t != texmap.end())
				glBindTexture(GL_TEXTURE_2D, (GLuint)t->second);
			else if (false)
				glBindTexture(GL_TEXTURE_2D, 0);
			else
				continue;
			glVertexPointer(3, GL_FLOAT, 12, part.vertices.data());
			glTexCoordPointer(2, GL_FLOAT, 8, part.texcoords.data());
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
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisable(GL_TEXTURE_2D);
	}
	else {
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnable(GL_TEXTURE_2D);
	}
	glColor4f(1, 1, 1, 1);
}

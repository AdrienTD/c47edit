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
#include <windows.h>
#include <GL/glew.h>
#include <GL/wglew.h>

#define glprocvalid(x) (! ( ((uintptr_t)x==-1) || (((uintptr_t)x >= 0) && ((uintptr_t)x <= 3)) ) )
typedef BOOL(APIENTRY *gli_wglSwapIntervalEXT)(int n);

HDC whdc; HGLRC glrc;
int drawframes = 0;
extern HWND hWindow;
bool rendertextures = true;
bool renderColorTextures = true, renderLightmaps = true;
bool enableAlphaTest = true;
bool renderUntexturedFaces = false;

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

std::map<const Mesh*, std::vector<Vector3>> g_skinnedMeshMap;

float* ApplySkinToMesh(const Mesh* mesh, Chunk* excChunk)
{
	auto [it,inserted] = g_skinnedMeshMap.try_emplace(mesh);
	if (!inserted)
		return (float*)it->second.data();

#pragma pack(push, 1)
	struct BonePre {
		uint16_t parentIndex, flags;
		double stuff[7];
		char name[16];
	};
	static_assert(sizeof(BonePre) == 0x4C);
#pragma pack(pop)

	if (Chunk* lche = excChunk->findSubchunk('LCHE')) {
		Chunk* hmtx = excChunk->findSubchunk('HMTX');
		Chunk* hpre = excChunk->findSubchunk('HPRE');
		Chunk* hpts = excChunk->findSubchunk('HPTS');
		Chunk* hpvd = excChunk->findSubchunk('HPVD');
		Chunk* vrmp = excChunk->findSubchunk('VRMP');
		assert(hmtx && hpre && hpts && vrmp && hpvd);
		uint32_t numBones = *(uint32_t*)lche->maindata.data();
		uint32_t numUsedVertices = *(uint32_t*)(lche->maindata.data() + 12);
		assert(hmtx->multidata.size() == numBones);

		// compute global matrix for each bone
		std::vector<std::pair<Matrix, std::string>> boneGlobal;
		boneGlobal.reserve(numBones);
		for (uint32_t i = 0; i < numBones; ++i) {
			int xxx = i;
			Matrix globalMtx = Matrix::getIdentity();
			std::string bonePath;
			while (xxx != 65535) {
				const BonePre* bone;
				if (hpre->maindata.size() > 0)
					bone = ((BonePre*)hpre->maindata.data()) + xxx;
				else
					bone = (BonePre*)hpre->multidata[0].data() + xxx;
				const double* dmtx = (double*)hmtx->multidata[xxx].data();
				Matrix boneMtx = Matrix::getIdentity();
				for (int row = 3; row >= 0; --row) {
					boneMtx.m[row][0] = (float)*(dmtx++);
					boneMtx.m[row][1] = (float)*(dmtx++);
					boneMtx.m[row][2] = (float)*(dmtx++);
				}
				globalMtx = globalMtx * boneMtx;
				bonePath += bone->name;
				bonePath += '/';
				xxx = bone->parentIndex;
			}
			boneGlobal.push_back({ globalMtx, std::move(bonePath) });
		}

		// working vector buffer
		std::vector<Vector3>& workBuffer = it->second;
		workBuffer.resize(mesh->getNumVertices());
		memcpy(workBuffer.data(), mesh->vertices.data(), 12 * mesh->getNumVertices());

		// transform each vertex with the global matrix of the corresponding bone
		const uint16_t* ptsRanges = (uint16_t*)hpts->maindata.data();
		for (uint32_t i = 0; i < numBones; ++i) {
			uint16_t startRange = (i == 0) ? 0 : ptsRanges[i - 1];
			uint16_t endRange = ptsRanges[i];
			for (uint16_t vtx = startRange; vtx < endRange; ++vtx)
				workBuffer[vtx] = workBuffer[vtx].transform(boneGlobal[i].first);
		}

		// apply HPVD
		const uint32_t* pvd = (uint32_t*)hpvd->maindata.data();
		bool the_end = false;
		while (!the_end) {
			uint32_t segStartInt = *pvd;
			float segStartFloat = *(float*)(pvd + 1);
			pvd += 2;
			Vector3& usedVec = workBuffer[segStartInt];
			usedVec *= segStartFloat;
			while (true) {
				uint32_t pntInt = *pvd;
				uint32_t pntIndex = pntInt & 0x3FFFFFFF;
				float pntFloat = *(float*)(pvd + 1);
				pvd += 2;
				usedVec += workBuffer[pntIndex] * pntFloat;
				if (pntInt & 0x80000000) {
					if (pntInt & 0x40000000)
						the_end = true;
					break;
				}
			}
		}

		// apply VRMP
		const uint32_t* remap = (uint32_t*)vrmp->maindata.data();
		uint32_t numSwaps = *(remap++);
		for (uint32_t i = 0; i < numSwaps; ++i) {
			uint32_t index1 = *(remap++);
			uint32_t index2 = *(remap++);
			assert((index1 % 3) == 0 && (index2 % 3) == 0);
			workBuffer[index1 / 3] = workBuffer[index2 / 3];
		}
	}
	return (float*)it->second.data();
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
	struct PartKey {
		uint16_t flags, texId, lgtId; bool invisible;

		static const uint16_t importantFlags = FTXFlag::opac | FTXFlag::wire | FTXFlag::add;
		PartKey(uint16_t texId, uint16_t lgtId, uint16_t flags) :
			flags(flags & importantFlags), texId(texId), lgtId(lgtId), invisible(!(flags & FTXFlag::textureMask)) {}
		auto asRefTuple() const { return std::tie(flags, texId, lgtId, invisible); }
		bool operator<(const PartKey& other) const { return asRefTuple() < other.asRefTuple(); }
		bool operator==(const PartKey& other) const { return asRefTuple() == other.asRefTuple(); }
	};
	std::map<PartKey, Part> parts;

	inline static std::map<Mesh*, ProMesh> g_proMeshes;

	// Get a prepared mesh from the cache, make one if not already done
	static ProMesh* getProMesh(Mesh* mesh, Chunk* excChunk) {
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

		if (excChunk && excChunk->findSubchunk('LCHE'))
			verts = ApplySkinToMesh(mesh, excChunk);

		float *uvCoords = (float*)defUvs;
		float *lgtCoords = (float*)defUvs;
		if (hasFtx) {
			uvCoords = (float*)mesh->textureCoords.data();
			lgtCoords = (float*)mesh->lightCoords.data();
		}

		uint32_t* colorMap = nullptr;
		if (!g_scene.lgtPack.subchunks.empty()) {
			assert(g_scene.lgtPack.subchunks[0].tag == 'RGBA');
			uint8_t* colorMapData = g_scene.lgtPack.subchunks[0].maindata.data();
			assert(*(uint16_t*)(colorMapData + 6) == 2); // the width of color map must be 2
			colorMapData += 0x14; // skip texture header until name
			while (*colorMapData++); // skip texture name
			colorMapData += 4; // skip mipmap size
			colorMap = (uint32_t*)colorMapData;
		}

		auto nextFace = [&](int shape, ProMesh::IndexType* indices) {
			bool isTextured = hasFtx && (ftxFace[0] & FTXFlag::textureMask);
			bool isLit = hasFtx && (ftxFace[0] & FTXFlag::lightMapMask);
			uint16_t texid = isTextured ? ftxFace[2] : 0xFFFF;
			uint16_t lgtid = isLit ? ftxFace[3] : 0xFFFF;
			auto& part = pro.parts[PartKey(texid, lgtid, ftxFace[0])];
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
				uint32_t color = (isLit && ftxFace[3] == 0xFFFF && colorMap) ? colorMap[4 * (ftxFace[5] - 1) + lgtit[j]] : 0xFFFFFFFF;
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

std::map<ProMesh::PartKey, std::vector<std::pair<Matrix, const ProMesh::Part*>>> g_meshLists;

void DrawMesh(Mesh* mesh, const Matrix& matrix, Chunk* excChunk)
{
	if (!rendertextures)
	{
		const float* vertices = mesh->vertices.data();
		if (excChunk && excChunk->findSubchunk('LCHE'))
			vertices = ApplySkinToMesh(mesh, excChunk);
		glLoadMatrixf(matrix.v);
		glVertexPointer(3, GL_FLOAT, 6, vertices);
		glDrawElements(GL_QUADS, mesh->quadindices.size(), GL_UNSIGNED_SHORT, mesh->quadindices.data());
		glDrawElements(GL_TRIANGLES, mesh->triindices.size(), GL_UNSIGNED_SHORT, mesh->triindices.data());
	}
	else
	{
		ProMesh* pro = ProMesh::getProMesh(mesh, excChunk);
		for (auto& [mat,part] : pro->parts) {
			if (!renderUntexturedFaces && mat.invisible)
				continue;
			g_meshLists[mat].push_back({ matrix, &part });
		}
	}
}

void RenderMeshLists()
{
	if (!rendertextures)
		return;
	for (auto& [mat, partList] : g_meshLists) {
		GLuint gltex = 0, gllgt = 0;
		if (renderColorTextures)
			if (auto t = texmap.find(mat.texId); t != texmap.end())
				gltex = (GLuint)(uintptr_t)t->second;
		if (renderLightmaps)
			if (auto t = texmap.find(mat.lgtId); t != texmap.end())
				gllgt = (GLuint)(uintptr_t)t->second;
		glActiveTextureARB(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, gltex);
		glActiveTextureARB(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, gllgt);
		if (enableAlphaTest && (mat.flags & FTXFlag::opac)) {
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GEQUAL, 0.1f);
		}
		else {
			glDisable(GL_ALPHA_TEST);
		}
		//if (flags & FTXFlag::add) {
		//	glEnable(GL_BLEND);
		//	glBlendFunc(GL_ONE, GL_ONE);
		//}
		//else {
		//	glDisable(GL_BLEND);
		//}
		for (auto& [matrix, partPtr] : partList) {
			auto& part = *partPtr;
			glVertexPointer(3, GL_FLOAT, 12, part.vertices.data());
			if (renderLightmaps)
				glColorPointer(4, GL_UNSIGNED_BYTE, 4, part.colors.data());
			glClientActiveTextureARB(GL_TEXTURE0);
			glTexCoordPointer(2, GL_FLOAT, 8, part.texcoords.data());
			glClientActiveTextureARB(GL_TEXTURE1);
			glTexCoordPointer(2, GL_FLOAT, 8, part.lightmapCoords.data());
			glLoadMatrixf(matrix.v);
			glDrawElements(GL_TRIANGLES, part.indices.size(), GL_UNSIGNED_SHORT, part.indices.data());
		}
	}
}

void InvalidateMesh(Mesh* mesh)
{
	ProMesh::g_proMeshes.erase(mesh);
	g_skinnedMeshMap.erase(mesh);
}

void UncacheAllMeshes()
{
	ProMesh::g_proMeshes.clear();
	g_skinnedMeshMap.clear();
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

		for (auto& [mat, list] : g_meshLists)
			list.clear();
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

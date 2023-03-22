#include "ModelImporter.h"

#include <cassert>
#include <filesystem>

#include "gameobj.h"
#include "texture.h"

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include <assimp/postprocess.h>

static std::string ShortenTextureName(std::string_view fullName)
{
	return std::filesystem::path(fullName).stem().string();
}

static uint32_t GetTextureFromAssimp(const aiTexture* atex, std::string_view name)
{
	if (Chunk* texChunk = FindTextureChunkByName(g_scene, name).first) {
		auto* ti = (TexInfo*)texChunk->maindata.data();
		return ti->id;
	}
	if (atex->mHeight != 0) {
		auto id = AddTexture(g_scene, (uint8_t*)atex->pcData, atex->mWidth, atex->mHeight, name);
		return id;
	}
	else {
		auto id = AddTexture(g_scene, atex->pcData, atex->mWidth, name);
		return id;
	}
}

Mesh ImportWithAssimp(const std::filesystem::path& filename)
{
	Mesh gmesh;
	Assimp::Importer importer;
	const aiScene* ais = importer.ReadFile(filename.string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_MakeLeftHanded);

	// Meshes
	for (unsigned int m = 0; m < ais->mNumMeshes; ++m) {
		auto& amesh = ais->mMeshes[m];

		// Find the texture, and import it if possible
		auto& mat = ais->mMaterials[amesh->mMaterialIndex];
		uint16_t texId = 0xFFFF;
		aiString aiTexName;
		aiReturn ret = mat->GetTexture(aiTextureType_DIFFUSE, 0, &aiTexName);
		if (ret == aiReturn_FAILURE)
			ret = mat->GetTexture(aiTextureType_BASE_COLOR, 0, &aiTexName);
		if (ret == aiReturn_SUCCESS) {
			if (const aiTexture* atex = ais->GetEmbeddedTexture(aiTexName.C_Str())) {
				// the texture is embedded in the file
				std::string tn;
				if (atex->mFilename.length > 0) {
					// texture has filename, take its stem
					tn = ShortenTextureName({ atex->mFilename.data, atex->mFilename.length });
				}
				else {
					// texture has no filename, take the material name instead
					auto aistr = mat->GetName();
					tn = { aistr.data, aistr.length };
				}
				texId = (uint16_t)GetTextureFromAssimp(atex, tn);
			}
			else
			{
				// sorry, but the texture is in another file
				std::filesystem::path texpath = std::filesystem::path(std::string_view{ aiTexName.data, aiTexName.length });
				texpath = filename.parent_path() / texpath.relative_path();
				std::string name = ShortenTextureName({ aiTexName.data, aiTexName.length });
				if (Chunk* chk = FindTextureChunkByName(g_scene, name).first) {
					// the texture is already imported, reuse it
					texId = (uint16_t)((TexInfo*)chk->maindata.data())->id;
				}
				else if (std::filesystem::is_regular_file(texpath)) {
					// the file exists, import it
					texId = (uint16_t)AddTexture(g_scene, texpath);
				}
			}
		}

		// Vertices
		uint16_t firstVtx = (uint16_t)(gmesh.vertices.size() / 3);
		gmesh.vertices.insert(gmesh.vertices.end(), (float*)amesh->mVertices, (float*)(amesh->mVertices + amesh->mNumVertices));

		// Faces (indices + UVs)
		bool hasTextureCoords = amesh->HasTextureCoords(0) && texId != 0xFFFF;
		for (unsigned int f = 0; f < amesh->mNumFaces; ++f) {
			auto& face = amesh->mFaces[f];
			if (face.mNumIndices < 3)
				continue;
			assert(face.mNumIndices == 3);

			std::array<uint16_t, 3> inds = {
				(uint16_t)((firstVtx + face.mIndices[0]) << 1),
				(uint16_t)((firstVtx + face.mIndices[1]) << 1),
				(uint16_t)((firstVtx + face.mIndices[2]) << 1)
			};
			gmesh.triindices.insert(gmesh.triindices.end(), inds.begin(), inds.end());

			std::array<uint16_t, 6> ftx = { (uint16_t)(hasTextureCoords ? 0x20 : 0), 0, texId, 0, 0, 0 };
			gmesh.ftxFaces.push_back(ftx);
			if (hasTextureCoords) {
				std::array<float, 8> uvs;
				uvs.fill(0.0f);
				for (size_t i = 0; i < 3; ++i) {
					auto& coord = amesh->mTextureCoords[0][face.mIndices[i]];
					uvs[2 * i] = coord.x;
					uvs[2 * i + 1] = coord.y;
				}
				gmesh.textureCoords.insert(gmesh.textureCoords.end(), uvs.begin(), uvs.end());
			}
		}
	}
	return gmesh;
}

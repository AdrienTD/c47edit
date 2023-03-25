#include "ModelImporter.h"

#include <cassert>
#include <filesystem>

#include "gameobj.h"
#include "texture.h"
#include "video.h"
#include "ByteWriter.h"
#include "chunk.h"

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Logger.hpp>
#include <assimp/DefaultLogger.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
extern HWND hWindow;

#pragma pack(push, 1)
struct BonePre {
	uint16_t parentIndex, flags;
	double stuff[7];
	char name[16];
};
static_assert(sizeof(BonePre) == 0x4C);
#pragma pack(pop)

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

std::optional<std::pair<Mesh, std::optional<Chunk>>> ImportWithAssimp(const std::filesystem::path& filename)
{
	Assimp::Importer importer;
	const aiScene* ais = importer.ReadFile(filename.u8string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_MakeLeftHanded | aiProcess_PopulateArmatureData | aiProcess_JoinIdenticalVertices);
	if (!ais) {
		std::string msg = "Assimp Import Error:\n";
		msg += importer.GetErrorString();
		MessageBoxA(hWindow, msg.c_str(), "Import Error", 16);
		return {};
	}

	Mesh gmesh;

	struct BoneInfo {
		Matrix transform;
		Matrix invBind;
		int numChildren;
		std::map<unsigned int, float> weights;
		int parent;
	};
	std::map<std::string, BoneInfo> boneMap;

	// Meshes
	std::map<aiVector3D, int> dupVertMap;
	int nextVertId = 0;
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
		std::vector<uint32_t> remap(amesh->mNumVertices);
		for (unsigned int v = 0; v < amesh->mNumVertices; ++v) {
			auto& avert = amesh->mVertices[v];
			auto [it, inserted] = dupVertMap.try_emplace(avert, nextVertId);
			remap[v] = it->second;
			if (inserted) {
				gmesh.vertices.insert(gmesh.vertices.end(), (float*)&avert.x, (float*)&avert.x + 3);
				nextVertId += 1;
			}
		}

		// Faces (indices + UVs)
		bool hasTextureCoords = amesh->HasTextureCoords(0) && texId != 0xFFFF;
		for (unsigned int f = 0; f < amesh->mNumFaces; ++f) {
			auto& face = amesh->mFaces[f];
			if (face.mNumIndices < 3)
				continue;
			assert(face.mNumIndices == 3);

			std::array<uint16_t, 3> inds = {
				(uint16_t)(remap[face.mIndices[0]] << 1),
				(uint16_t)(remap[face.mIndices[1]] << 1),
				(uint16_t)(remap[face.mIndices[2]] << 1)
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

		// Bones
		for (unsigned int b = 0; b < amesh->mNumBones; ++b) {
			aiBone* abone = amesh->mBones[b];
			auto [it, inserted] = boneMap.try_emplace(abone->mName.C_Str());
			auto& ws = it->second;
			if (inserted) {
				memcpy(&ws.transform, &abone->mNode->mTransformation, 64);
				memcpy(&ws.invBind, &abone->mOffsetMatrix, 64);
				ws.transform = ws.transform.getTranspose();
				ws.invBind = ws.invBind.getTranspose();
				ws.numChildren = abone->mNode->mNumChildren;
			}
			for (unsigned int w = 0; w < abone->mNumWeights; ++w)
				if (abone->mWeights[w].mWeight > 0.0f)
					ws.weights[remap[abone->mWeights[w].mVertexId]] = abone->mWeights[w].mWeight;
		}
	}
	if(boneMap.empty())
		return std::make_pair(std::move(gmesh), std::nullopt);
	else {
		// find the root bone node
		auto findRootBoneNode = [&boneMap](aiNode* node, const auto& rec) -> aiNode* {
			if (boneMap.count(node->mName.C_Str()))
				return node;
			for (size_t i = 0; i < node->mNumChildren; ++i)
				if (aiNode* c = rec(node->mChildren[i], rec))
					return c;
			return nullptr;
		};
		aiNode* rootBone = findRootBoneNode(ais->mRootNode, findRootBoneNode);
		printf("Root bone found: %s\n", rootBone->mName.C_Str());
		
		// construct vector of BoneInfo* in order of hierarchy
		std::vector<std::pair<const std::string*, BoneInfo*>> boneInfos;
		auto walkBoneNode = [&boneMap,&boneInfos](aiNode* node, int parent, const auto& rec) -> void {
			auto it = boneMap.find(node->mName.C_Str());
			assert(it != boneMap.end());
			int id = boneInfos.size();
			it->second.parent = parent;
			boneInfos.emplace_back(&it->first, &it->second);
			for (size_t i = 0; i < node->mNumChildren; ++i)
				rec(node->mChildren[i], id, rec);
		};
		walkBoneNode(rootBone, -1, walkBoneNode);

		Chunk excChunk;
		excChunk.tag = 'HEAD';
		excChunk.subchunks.resize(6);
		Chunk& lche = excChunk.subchunks[0]; lche.tag = 'LCHE';	// OK
		Chunk& hmtx = excChunk.subchunks[1]; hmtx.tag = 'HMTX';	// OK
		Chunk& hpts = excChunk.subchunks[2]; hpts.tag = 'HPTS';	// OK
		Chunk& hpvd = excChunk.subchunks[3]; hpvd.tag = 'HPVD';	// OK
		Chunk& hpre = excChunk.subchunks[4]; hpre.tag = 'HPRE';	// OK
		//Chunk& hpmo = excChunk.subchunks[1]; hpmo.tag = 'HPMO';
		Chunk& vrmp = excChunk.subchunks[5]; vrmp.tag = 'VRMP';	// OK

		int boneMap;

		uint32_t numBones = boneInfos.size();
		uint32_t numOriginalVertices = gmesh.getNumVertices();

		// HPTS: Bone vertex ranges

		uint16_t numWorkVertices = 0;
		std::vector<float> workVertices;
		std::map<uint32_t, std::vector<std::pair<uint32_t, float>>> oriToWorkMap;
		hpts.maindata.resize(2 * numBones);
		uint16_t* hptsPtr = (uint16_t*)hpts.maindata.data();
		for (auto& [boneName,boneInfo] : boneInfos) {
			for (auto& [oriVertexIndex, wWeight] : boneInfo->weights) {
				auto it = gmesh.vertices.data() + 3 * oriVertexIndex;
				Vector3 vec{ it[0], it[1], it[2] };
				vec = vec.transform(boneInfo->invBind);
				uint16_t workVertIndex = workVertices.size() / 3;
				workVertices.insert(workVertices.end(), (float*)vec.coord, (float*)vec.coord + 3);
				oriToWorkMap[oriVertexIndex].emplace_back(workVertIndex, wWeight);
			}
			numWorkVertices += (uint16_t)boneInfo->weights.size(); // What if someone tries a model with > 64Ki weights?
			*hptsPtr++ = numWorkVertices;
		}
		assert(workVertices.size() == 3 * numWorkVertices);
		printf("numWorkVertices=%u, numOriginalVertices=%u\n", numWorkVertices, numOriginalVertices);

		// replace mesh vertices with "work buffer"
		gmesh.vertices = workVertices;

		// LCHE: Header

		lche.maindata.resize(16);
		uint32_t* lchePtr = (uint32_t*)lche.maindata.data();
		*lchePtr++ = numBones;
		*lchePtr++ = numWorkVertices;
		*lchePtr++ = numWorkVertices;
		*lchePtr++ = numOriginalVertices;

		// HMTX: Bone transform matrices

		hmtx.multidata.resize(numBones);
		uint32_t boneIndex = 0;
		for (auto& [boneName, boneInfo] : boneInfos) {
			hmtx.multidata[boneIndex].resize(sizeof(double) * 4 * 3);
			double* mat = (double*)hmtx.multidata[boneIndex].data();
			for (int row = 3; row >= 0; --row) {
				*mat++ = (double)boneInfo->transform.m[row][0];
				*mat++ = (double)boneInfo->transform.m[row][1];
				*mat++ = (double)boneInfo->transform.m[row][2];
			}
			boneIndex += 1;
		}

		// HPRE: Bone info

		hpre.maindata.resize(sizeof(BonePre)* numBones);
		BonePre* hprePtr = (BonePre*)hpre.maindata.data();
		for (auto& [boneName, boneInfo] : boneInfos) {
			memset(hprePtr->name, 0, sizeof(hprePtr->name));
			std::copy_n(boneName->data(), std::min(boneName->size(), (size_t)15), hprePtr->name);
			hprePtr->parentIndex = (uint16_t)boneInfo->parent;
			hprePtr->flags = (boneInfo->numChildren == 1) ? 0 : ((boneInfo->numChildren == 0) ? 1 : 2);
			for (double& d : hprePtr->stuff)
				d = 0.0;
			++hprePtr;
		}

		// HPVD: Weigting

		ByteWriter<std::vector<uint8_t>> hpvdBytes;
		auto* lastElem = &oriToWorkMap.rbegin()->first;
		for (auto& [oriVertIndex, mWorkVerts] : oriToWorkMap) {
			if (mWorkVerts.size() >= 2) { // ignore vertices with 0 or 1 weight
				size_t last = mWorkVerts.size() - 1;
				for (size_t i = 0; i < mWorkVerts.size(); ++i) {
					auto& [index,weight] = mWorkVerts[i];
					uint32_t flaggedIndex = index;
					if (i == last) flaggedIndex |= 0x80000000;
					hpvdBytes.addU32(flaggedIndex);
					hpvdBytes.addFloat(weight);
				}
			}
		}
		auto hpvdVector = hpvdBytes.take();
		assert(hpvdVector.size() >= 16);
		assert(*(uint32_t*)(hpvdVector.data() + hpvdVector.size() - 8) & 0x80000000);
		*(uint32_t*)(hpvdVector.data() + hpvdVector.size() - 8) |= 0xC0000000;
		hpvd.maindata.resize(hpvdVector.size());
		memcpy(hpvd.maindata.data(), hpvdVector.data(), hpvdVector.size());

		// VRMP: Vertex remapping

		static constexpr uint16_t novalue = 0xFFFF;
		std::vector<uint16_t> k_eq_v_Map(numWorkVertices, novalue);
		std::vector<uint16_t> v_eq_k_Map(numWorkVertices, novalue);
		for (auto& [oriVertIndex, mWorkVerts] : oriToWorkMap) {
			if (oriVertIndex != mWorkVerts.at(0).first) { // -> no self-loop
				assert(k_eq_v_Map[oriVertIndex] == novalue);
				k_eq_v_Map[oriVertIndex] = mWorkVerts.at(0).first;
				assert(v_eq_k_Map[mWorkVerts.at(0).first] == novalue);
				v_eq_k_Map[mWorkVerts.at(0).first] = oriVertIndex;
			}
		}
		std::vector<std::pair<uint32_t, uint32_t>> remaps;
		// for every start of chain component
		for (uint16_t searchRight = 0; searchRight < numWorkVertices; ++searchRight) {
			uint16_t searchLeft = v_eq_k_Map[searchRight];
			if (searchLeft == novalue) {
				// iterate the chain
				uint16_t left = searchRight;
				uint16_t right = k_eq_v_Map[left];
				while (right != novalue) {
					remaps.emplace_back((uint32_t)left * 3, (uint32_t)right * 3);
					left = right;
					right = k_eq_v_Map[right];
				}
			}
		}
		vrmp.maindata.resize(4 + remaps.size() * 8);
		uint32_t* vrmpPtr = (uint32_t*)vrmp.maindata.data();
		*vrmpPtr++ = (uint32_t)remaps.size();
		for (auto& [a, b] : remaps) {
			*vrmpPtr++ = a;
			*vrmpPtr++ = b;
		}



		return std::make_pair(std::move(gmesh), std::move(excChunk));
	}
}

void ExportWithAssimp(const Mesh& gmesh, const std::filesystem::path& filename, Chunk* excChunk)
{
	aiScene ascene;

	struct Part {
		std::vector<aiVector3D> vertices;
		std::vector<aiVector3D> texCoords;
		std::vector<aiFace> faces;
		unsigned int primitiveTypes = 0;
		std::map<int, std::vector<aiVertexWeight>> weights;
	};
	using PartKey = uint16_t;
	std::map<PartKey, Part> parts;

	bool hasBones = excChunk && excChunk->findSubchunk('LCHE');
	
	const float* vertices = gmesh.vertices.data();
	if (hasBones) {
		vertices = ApplySkinToMesh(&gmesh, excChunk);
	}

	std::vector<std::vector<std::pair<uint32_t, float>>> mapVertToWeights;
	std::map<uint16_t, uint16_t> mapFinalVertToWorkVert;
	const BonePre* gbones = nullptr;
	uint32_t numBones = 0;
	if (hasBones) {

		Chunk* lche = excChunk->findSubchunk('LCHE');
		Chunk* hmtx = excChunk->findSubchunk('HMTX');
		Chunk* hpre = excChunk->findSubchunk('HPRE');
		Chunk* hpts = excChunk->findSubchunk('HPTS');
		Chunk* hpvd = excChunk->findSubchunk('HPVD');
		Chunk* vrmp = excChunk->findSubchunk('VRMP');
		assert(hmtx&& hpre&& hpts&& vrmp&& hpvd);
		numBones = *(uint32_t*)lche->maindata.data();
		uint32_t numUsedVertices = *(uint32_t*)(lche->maindata.data() + 12);
		assert(hmtx->multidata.size() == numBones);

		if (hpre->maindata.size() > 0)
			gbones = (const BonePre*)hpre->maindata.data();
		else
			gbones = (const BonePre*)hpre->multidata[0].data();

		const uint16_t* ptsRanges = (uint16_t*)hpts->maindata.data();
		const uint16_t* ptsRangesEnd = ptsRanges + numBones;
		auto getWorkVertexBone = [&](uint16_t index) {
			return std::upper_bound(ptsRanges, ptsRangesEnd, index) - ptsRanges;
		};

		uint32_t* remapPtr = (uint32_t*)vrmp->maindata.data();
		uint32_t numRemaps = *remapPtr++;
		for (uint32_t i = 0; i < numRemaps; ++i) {
			mapFinalVertToWorkVert[remapPtr[0] / 3] = remapPtr[1] / 3;
			remapPtr += 2;
		}

		mapVertToWeights.resize(gmesh.getNumVertices());
		const uint32_t* pvd = (uint32_t*)hpvd->maindata.data();
		bool the_end = false;
		while (!the_end) {
			uint32_t segStartInt = *pvd;
			float segStartFloat = *(float*)(pvd + 1);
			auto& vec = mapVertToWeights[segStartInt];
			assert(vec.empty());
			vec.emplace_back(getWorkVertexBone(segStartInt), segStartFloat);
			pvd += 2;
			while (true) {
				uint32_t pntInt = *pvd;
				uint32_t pntIndex = pntInt & 0x3FFFFFFF;
				float pntFloat = *(float*)(pvd + 1);
				vec.emplace_back(getWorkVertexBone(pntIndex), pntFloat);
				pvd += 2;
				if (pntInt & 0x80000000) {
					if (pntInt & 0x40000000)
						the_end = true;
					break;
				}
			}
		}
		for (size_t v = 0; v < gmesh.getNumVertices(); ++v) {
			auto& vec = mapVertToWeights[v];
			if (vec.empty())
				vec.emplace_back(getWorkVertexBone(v), 1.0f);
		}
	}

	const Mesh::FTXFace* ftxptr = gmesh.ftxFaces.data();
	const float* uvptr = gmesh.textureCoords.data();
	for (auto [indvec, shape] : { std::make_pair(&gmesh.triindices, 3u), std::make_pair(&gmesh.quadindices, 4u) }) {
		const uint16_t* indptr = indvec->data();
		size_t numFaces = indvec->size() / shape;
		for (size_t f = 0; f < numFaces; ++f) {
			auto& ftx = *ftxptr;
			uint16_t texid = (ftx[0] & 0x20) ? ftx[2] : 0xFFFF;
			auto& part = parts[texid];

			unsigned int facesFirstVertexIndex = (unsigned int)part.vertices.size();
			for (unsigned int i = 0; i < shape; ++i) {
				const float* gvec = vertices + 3 * (indptr[i] >> 1);
				part.vertices.emplace_back(gvec[0], gvec[1], gvec[2]);
			}

			aiFace& face = part.faces.emplace_back();
			face.mNumIndices = shape;
			face.mIndices = new unsigned int[shape];
			for (unsigned int i = 0; i < shape; ++i)
				face.mIndices[i] = facesFirstVertexIndex + i;

			if (ftx[0] & 0x20) {
				for (unsigned int i = 0; i < shape; ++i) {
					part.texCoords.emplace_back(uvptr[2 * i], uvptr[2 * i + 1], 0.0f);
				}
				uvptr += 8;
			}

			part.primitiveTypes |= (shape == 3) ? aiPrimitiveType_TRIANGLE : aiPrimitiveType_POLYGON;

			if (hasBones) {
				for (unsigned int i = 0; i < shape; ++i) {
					uint16_t finalIndex = indptr[i] >> 1;
					uint16_t workIndex = finalIndex;
					if (auto it = mapFinalVertToWorkVert.find(finalIndex); it != mapFinalVertToWorkVert.end())
						workIndex = it->second;
					for (auto& [boneId, weight] : mapVertToWeights.at(workIndex))
						part.weights[boneId].emplace_back(facesFirstVertexIndex + i, weight);
				}
			}

			indptr += shape;
			ftxptr += 1;
		}
	}

	ascene.mNumMeshes = parts.size();
	ascene.mMeshes = new aiMesh * [ascene.mNumMeshes];
	ascene.mNumMaterials = parts.size();
	ascene.mMaterials = new aiMaterial * [ascene.mNumMaterials];
	size_t m = 0;
	std::vector<Chunk*> texturesToExport;
	for (auto& [texid, part] : parts) {
		aiMesh* amesh = new aiMesh;
		ascene.mMeshes[m] = amesh;
		amesh->mMaterialIndex = m;

		amesh->mPrimitiveTypes = part.primitiveTypes;

		amesh->mNumVertices = part.vertices.size();
		amesh->mVertices = new aiVector3D[amesh->mNumVertices];
		memcpy(amesh->mVertices, part.vertices.data(), amesh->mNumVertices * sizeof(aiVector3D));

		if (texid != 0xFFFF) {
			amesh->mNumUVComponents[0] = 2;
			amesh->mTextureCoords[0] = new aiVector3D[amesh->mNumVertices];
			memcpy(amesh->mTextureCoords[0], part.texCoords.data(), part.texCoords.size() * sizeof(aiVector3D));
		}

		amesh->mNumFaces = part.faces.size();
		amesh->mFaces = new aiFace[amesh->mNumFaces];
		memcpy(amesh->mFaces, part.faces.data(), part.faces.size() * sizeof(aiFace));
		memset(part.faces.data(), 0, part.faces.size() * sizeof(aiFace)); // need to nullify the index pointers on old vector to keep them unique

		if (hasBones) {
			amesh->mNumBones = part.weights.size();
			amesh->mBones = new aiBone * [amesh->mNumBones];
			size_t b = 0;
			for (auto& [boneId,weights] : part.weights) {
				aiBone* abone = new aiBone;
				amesh->mBones[b++] = abone;
				abone->mName = gbones[boneId].name;
				abone->mNumWeights = weights.size();
				abone->mWeights = new aiVertexWeight[abone->mNumWeights];
				memcpy(abone->mWeights, weights.data(), weights.size() * sizeof(aiVertexWeight));

				int xxx = boneId;
				Matrix globalMtx = Matrix::getIdentity();
				Chunk* hmtx = excChunk->findSubchunk('HMTX');
				while (xxx != 65535) {
					const BonePre* bone = gbones + xxx;
					const double* dmtx = (double*)hmtx->multidata[xxx].data();
					Matrix boneMtx = Matrix::getIdentity();
					for (int row = 3; row >= 0; --row) {
						boneMtx.m[row][0] = (float)*(dmtx++);
						boneMtx.m[row][1] = (float)*(dmtx++);
						boneMtx.m[row][2] = (float)*(dmtx++);
					}
					globalMtx = globalMtx * boneMtx;
					xxx = bone->parentIndex;
				}
				Matrix invMtx = globalMtx.getInverse4x3().getTranspose();
				static_assert(sizeof(abone->mOffsetMatrix) == 64);
				memcpy(&abone->mOffsetMatrix, &invMtx, 64);

			}
		}

		aiMaterial* amat = new aiMaterial;
		ascene.mMaterials[m] = amat;
		aiString matName;
		aiString texPath;
		if (Chunk* texChunk = FindTextureChunk(g_scene, texid).first) {
			matName.Set(((TexInfo*)texChunk->maindata.data())->name);
			texPath.Set(std::string("*") + std::to_string(texturesToExport.size()));
			texturesToExport.push_back(texChunk);
			amat->AddProperty(&texPath, AI_MATKEY_TEXTURE_DIFFUSE(0));
		}
		else {
			matName.Set("Unnamed");
		}
		amat->AddProperty(&matName, AI_MATKEY_NAME);

		m += 1;
	}

	ascene.mNumTextures = texturesToExport.size();
	ascene.mTextures = new aiTexture * [ascene.mNumTextures];
	for (unsigned int t = 0; t < ascene.mNumTextures; ++t) {
		aiTexture* atex = new aiTexture;
		ascene.mTextures[t] = atex;
		
		Chunk* texChunk = texturesToExport[t];
		const TexInfo* ti = (const TexInfo*)texChunk->maindata.data();
		auto png = ExportTextureToPNGInMemory(texChunk);

		atex->mWidth = png.size();
		atex->mHeight = 0;
		atex->mFilename = ti->getName();
		static constexpr char hint[4] = "png";
		std::copy(std::begin(hint), std::end(hint), atex->achFormatHint);
		atex->pcData = new aiTexel[(png.size() + 3) / 4];
		memcpy(atex->pcData, png.data(), png.size());
	}

	ascene.mRootNode = new aiNode;
	ascene.mRootNode->mNumMeshes = parts.size();
	ascene.mRootNode->mMeshes = new unsigned int[parts.size()];
	for (size_t i = 0; i < parts.size(); ++i)
		ascene.mRootNode->mMeshes[i] = i;
	if (hasBones) {
		std::vector<aiNode*> boneNodes;
		std::map<aiNode*, std::vector<aiNode*>> boneChildNodes;
		boneNodes.resize(numBones);
		Chunk* hmtx = excChunk->findSubchunk('HMTX');
		for (size_t b = 0; b < numBones; ++b) {
			aiNode* node = new aiNode;
			boneNodes[b] = node;
			node->mName = gbones[b].name;
			if (gbones[b].parentIndex != 0xFFFF)
				node->mParent = boneNodes[gbones[b].parentIndex];
			else
				node->mParent = ascene.mRootNode;
			boneChildNodes[node->mParent].push_back(node);
			const double* dmtx = (double*)hmtx->multidata[b].data();
			Matrix boneMtx = Matrix::getIdentity();
			for (int row = 3; row >= 0; --row) {
				boneMtx.m[row][0] = (float)*(dmtx++);
				boneMtx.m[row][1] = (float)*(dmtx++);
				boneMtx.m[row][2] = (float)*(dmtx++);
			}
			boneMtx = boneMtx.getTranspose();
			memcpy(&node->mTransformation, &boneMtx, 64);
		}
		for (auto& [node, children] : boneChildNodes) {
			node->mNumChildren = children.size();
			node->mChildren = new aiNode * [children.size()];
			memcpy(node->mChildren, children.data(), children.size() * sizeof(aiNode*));
		}
	}

	auto fileext = filename.extension().string().substr(1);
	std::transform(fileext.begin(), fileext.end(), fileext.begin(), [](char c) {return (char)std::tolower(c); });
	if (fileext == "gltf" || fileext == "glb")
		fileext += '2';
	if (fileext == "dae")
		fileext = "collada";

	Assimp::Exporter exporter;
	aiReturn res = exporter.Export(&ascene, fileext, filename.u8string(), aiProcess_FlipUVs | aiProcess_MakeLeftHanded);
	if (res != aiReturn_SUCCESS) {
		std::string msg = "Assimp Export Error:\n";
		msg += exporter.GetErrorString();
		MessageBoxA(hWindow, msg.c_str(), "Export Error", 16);
	}
}
// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "chunk.h"
#include "vecmat.h"

struct GameObject;
struct Chunk;
struct Scene;

class GORef
{
private:
	GameObject * m_obj = nullptr;
public:
	GameObject * get() const noexcept { return m_obj; }
	bool valid() const noexcept { return m_obj; }
	GameObject* operator->() const noexcept { return m_obj; }
	explicit operator bool() const noexcept { return valid(); }

	void set(GameObject* obj) noexcept;
	void deref() noexcept;
	void operator=(const GORef& ref) noexcept { set(ref.m_obj); }
	void operator=(GORef&& ref) noexcept { deref(); m_obj = ref.m_obj; ref.m_obj = nullptr; }
	void operator=(GameObject* obj) noexcept { set(obj); }

	GORef() noexcept : m_obj(nullptr) {}
	GORef(const GORef& ref) noexcept { set(ref.m_obj); }
	GORef(GORef&& ref) noexcept { m_obj = ref.m_obj; ref.m_obj = nullptr; }
	GORef(GameObject* obj) noexcept { set(obj); }
	~GORef() noexcept { deref(); }
};

struct Mesh
{
	std::vector<float> vertices;
	std::vector<uint16_t> quadindices, triindices;
	uint32_t weird;

	// FTX
	using FTXFace = std::array<uint16_t, 6>;
	std::vector<float> textureCoords;
	std::vector<float> lightCoords;
	std::vector<FTXFace> ftxFaces;

	struct Extension {
		uint32_t extUnk2;
		std::vector<std::pair<uint32_t, uint32_t>> frames;
		std::string name;
	};
	std::shared_ptr<Extension> extension; // TODO deep copy would be better

	size_t getNumVertices() const { return vertices.size() / 3u; }
	size_t getNumQuads() const { return quadindices.size() / 4u; }
	size_t getNumTris() const { return triindices.size() / 3u; }
};

struct ObjLine
{
	std::vector<float> vertices;
	std::vector<uint32_t> terms; // sum = num vertices
	uint32_t ftxo, weird;

	size_t getNumVertices() const { return vertices.size() / 3u; }
};

struct Light
{
	uint32_t param[7];
};

struct DBLEntry;
struct SceneSaver;

struct DBLList {
	int flags = 0;
	std::vector<DBLEntry> entries;

	void load(uint8_t* ptr, const std::map<uint32_t, GameObject*>& idobjmap);
	std::string save(SceneSaver& sceneSaver);
};

struct DBLEntry
{
	int type = 0;
	int flags = 0;
	using VariantType = std::variant<std::monostate, double, float, uint32_t, std::string, std::vector<uint8_t>, GORef, std::vector<GORef>, DBLList>;
	VariantType value;

	static const char* getTypeName(int type);
};

struct GameObject
{
	uint32_t state = 0;
	std::string name;
	Matrix matrix = Matrix::getIdentity();
	Vector3 position = Vector3(0.0f, 0.0f, 0.0f);
	uint32_t type = 0, flags = 0;

	std::vector<GameObject*> subobj;
	GameObject* parent = nullptr;
	GameObject* root = nullptr;

	// Mesh
	std::shared_ptr<Mesh> mesh;
	std::shared_ptr<ObjLine> line;
	uint32_t color = 0;

	std::shared_ptr<Light> light;

	DBLList dbl;
	std::shared_ptr<Chunk> excChunk;

	uint32_t refcount = 0;

	GameObject(const char *nName = "Unnamed", int nType = 0) : name(nName), type(nType) {}
	GameObject(const GameObject& other) = default;
	~GameObject() = default;

	std::string getPath() const;
};

inline void GORef::deref() noexcept { if (m_obj) { m_obj->refcount--; m_obj = nullptr; } }
inline void GORef::set(GameObject * obj) noexcept { deref(); m_obj = obj; if (m_obj) m_obj->refcount++; }

struct Scene {
	Chunk* spkchk, * prot, * pclp, * phea, * pnam, * ppos, * pmtx, * pver, * pfac, * pftx, * puvc, *pdbl, *pdat, *pexc;
	GameObject* rootobj, * cliprootobj, * superroot;
	std::string lastspkfn;
	void* zipmem = nullptr;
	uint32_t zipsize = 0;
	Chunk palPack, dxtPack, lgtPack, anmPack, wavPack;
	bool hasAnmPack = false;

	void LoadSceneSPK(const char *fn);
	void ModifySPK();
	void SaveSceneSPK(const char *fn);
	void RemoveObject(GameObject *o);
	GameObject* DuplicateObject(GameObject *o, GameObject *parent = nullptr);
	void GiveObject(GameObject *o, GameObject *t);
};
extern Scene g_scene;

const char* GetObjTypeString(uint32_t ot);

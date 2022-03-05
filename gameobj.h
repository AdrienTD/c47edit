// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "vecmat.h"

struct GameObject;
struct Chunk;

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
	//float *vertices;
	//uint16_t *quadindices, *triindices;
	uint32_t vertstart, quadstart, tristart, ftxo, numverts, numquads, numtris, weird;
	void Mesh::draw(Vector3* animatedVertices = nullptr);
};

struct Light
{
	uint32_t param[7];
};

struct DBLEntry
{
	int type = 0;
	int flags = 0;
	using VariantType = std::variant<std::monostate, double, float, uint32_t, std::string, std::vector<uint8_t>, GORef, std::vector<GORef>>;
	VariantType value;

	static const char* getTypeName(int type);
};

struct GameObject
{
	uint32_t refcount = 0;

	uint32_t state = 0;
	uint32_t pdbloff = 0, pexcoff = 0;
	std::string name;
	Matrix matrix = Matrix::getIdentity();
	Vector3 position = Vector3(0.0f, 0.0f, 0.0f);
	uint32_t type = 0, flags = 0;

	std::vector<GameObject*> subobj;
	GameObject* parent = nullptr;
	GameObject* root = nullptr;

	// Mesh
	Mesh* mesh = nullptr;
	uint32_t color = 0;

	Light *light = nullptr;

	std::vector<DBLEntry> dbl;
	uint32_t dblflags = 0;

	std::shared_ptr<Chunk> pexcChunk;

	GameObject(const char *nName = "Unnamed", int nType = 0) : name(nName), type(nType) {}
	GameObject(const GameObject& other) = default;
	~GameObject() = default;
};

inline void GORef::deref() noexcept { if (m_obj) { m_obj->refcount--; m_obj = nullptr; } }
inline void GORef::set(GameObject * obj) noexcept { deref(); m_obj = obj; if (m_obj) m_obj->refcount++; }

extern Chunk *spkchk, *prot, *pclp, *phea, *pnam, *ppos, *pmtx, *pver, *pfac, *pftx, *puvc;
extern GameObject *rootobj, *cliprootobj, *superroot;
extern std::string lastspkfn;
extern void *zipmem;
extern uint32_t zipsize;

const char* GetObjTypeString(uint32_t ot);
void LoadSceneSPK(const char *fn);
void ModifySPK();
void SaveSceneSPK(const char *fn);
void RemoveObject(GameObject *o);
GameObject* DuplicateObject(GameObject *o, GameObject *parent = rootobj);
void GiveObject(GameObject *o, GameObject *t);

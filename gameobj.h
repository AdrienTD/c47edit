// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "vecmat.h"

struct GameObject;
struct Chunk;

class goref
{
private:
	GameObject * obj = nullptr;
public:
	GameObject * get() const { return obj; }

	void deref();

	void set(GameObject* n);

	bool valid() const { return obj; }

	goref() : obj(nullptr) {}
	goref(GameObject* obj) { set(obj); }
	GameObject* operator->() { return obj; }
	void operator=(GameObject* o) { set(o); }
};

struct Mesh
{
	//float *vertices;
	//uint16_t *quadindices, *triindices;
	uint32_t vertstart, quadstart, tristart, ftxo, numverts, numquads, numtris, weird;
	void Mesh::draw();
};

struct Light
{
	uint32_t param[7];
};

struct DBLEntry
{
	int type = 0;
	int flags = 0;
	using VariantType = std::variant<std::monostate, double, float, uint32_t, std::string, std::vector<uint8_t>, goref, std::vector<goref>>;
	VariantType value;
};

struct GameObject
{
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

	uint32_t refcount = 0;

	GameObject(const char *nName = "Unnamed", int nType = 0) : name(nName), type(nType) {}
	GameObject(const GameObject& other) = default;
	~GameObject() = default;
};

inline void goref::deref() { if (obj) { obj->refcount--; obj = 0; } }
inline void goref::set(GameObject * n) { deref(); obj = n; if (obj) obj->refcount++; }

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

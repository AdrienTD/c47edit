// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

struct GameObject;

class goref
{
private:
	GameObject * obj;
public:
	GameObject * get() const { return obj; }

	void deref();

	void set(GameObject *n);

	bool valid() const { return obj; }

	goref() : obj(0) {}
	GameObject *operator->() { return obj; }
	void operator=(GameObject* o) { set(o); }
};

struct Mesh
{
	//float *vertices;
	//uint16_t *quadindices, *triindices;
	uint vertstart, quadstart, tristart, ftxo, numverts, numquads, numtris, weird;
	void Mesh::draw();
};

struct Light
{
	uint32_t param[7];
};

struct DBLEntry
{
	int type;
	int flags;
	union {
		double dbl;
		float flt;
		uint32_t u32;
		char *str;
		struct {
			size_t datsize;
			void *datpnt;
		};
		goref obj;
		struct {
			uint nobjs;
			goref *objlist;
		};
	};
	DBLEntry() : type(0), flags(0), datsize(0), datpnt(0) {}
};

struct GameObject
{
	uint state;
	uint pdbloff, pexcoff;
	char *name;
	Matrix matrix;
	Vector3 position;
	uint type, flags;

	std::vector<GameObject*> subobj;
	GameObject *parent;
	GameObject *root;

	// Mesh
	Mesh *mesh;
	uint color;

	Light *light;

	std::vector<DBLEntry> dbl;
	uint32_t dblflags;

	uint refcount;

	GameObject(char *nName = "Unnamed", int nType = 0) : name(strdup(nName)), type(nType),
		pdbloff(0), pexcoff(0), flags(0), mesh(0), color(0), position(0,0,0), light(0), state(0), parent(0), root(0),
		refcount(0)
	{
		CreateIdentityMatrix(&matrix);
	}
	~GameObject() { free(name); }
};

inline void goref::deref() { if (obj) { obj->refcount--; obj = 0; } }
inline void goref::set(GameObject * n) { deref(); obj = n; if (obj) obj->refcount++; }

extern Chunk *spkchk, *prot, *pclp, *phea, *pnam, *ppos, *pmtx, *pver, *pfac, *pftx, *puvc;
extern GameObject *rootobj, *cliprootobj, *superroot;
extern char *lastspkfn;
extern void *zipmem;
extern uint zipsize;

char* GetObjTypeString(uint ot);
void LoadSceneSPK(char *fn);
void ModifySPK();
void SaveSceneSPK(char *fn);
void RemoveObject(GameObject *o);
GameObject* DuplicateObject(GameObject *o, GameObject *parent = rootobj);
void GiveObject(GameObject *o, GameObject *t);

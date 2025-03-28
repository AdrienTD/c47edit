// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "chunk.h"
#include "vecmat.h"
#include "AudioManager.h"

struct GameObject;
struct Chunk;
struct Scene;

namespace ClassInfo {
	struct ObjectMember;
}

extern std::unordered_map<GameObject*, size_t> g_objRefCounts;

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

namespace FTXFlag
{
	static const int trans = 0x0001;
	static const int add = 0x0002;
	static const int sub = 0x0004;
	static const int wire = 0x0008;
	static const int texturePoint = 0x0010;
	static const int textureBilinear = 0x0020;
	static const int textureMask = texturePoint | textureBilinear;
	static const int lightMapPoint = 0x0040;
	static const int lightMapBilinear = 0x0080;
	static const int lightMapMask = lightMapPoint | lightMapBilinear;
	static const int gouraud = 0x0100;
	static const int opac = 0x0200;
	static const int opac2 = 0x0400;
	static const int mirror = 0x0800;
	static const int refl = 0x1000;
	static const int shadow = 0x2000;
	static const int refrac = 0x4000;
};

struct Mesh
{
	std::vector<float> vertices;
	std::vector<uint16_t> quadindices, triindices;
	uint32_t weird = 0;

	// FTX
	using FTXFace = std::array<uint16_t, 6>;
	std::vector<float> textureCoords;
	std::vector<float> lightCoords;
	std::vector<FTXFace> ftxFaces;

	struct Extension {
		uint32_t type;
		struct TextureAnimation {
			std::vector<std::pair<uint32_t, uint32_t>> frames;
			std::string name;
		};
		std::array<TextureAnimation, 2> texAnims;
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
	void addMembers(const std::vector<ClassInfo::ObjectMember>& members);
};

struct DBLEntry
{
	enum class EType : int {
		UNDEFINED = 0,
		DOUBLE = 1,
		FLOAT = 2,
		INT = 3,
		STRING = 4,
		FILE = 5,
		TERMINATOR = 6,
		DATA = 7,
		ZGEOMREF = 8,
		ZGEOMREFTAB = 9,
		MSG = 10,
		SNDREF = 11,
		SCRIPT = 12
	};

	EType type = EType::UNDEFINED;
	int flags = 0;
	using VariantType = std::variant<std::monostate, double, float, uint32_t, std::string, std::vector<uint8_t>, GORef, std::vector<GORef>, DBLList, AudioRef>;
	VariantType value;

	static const char* getTypeName(int type);
};

struct GameObject
{
	std::string name;
	Matrix matrix = Matrix::getIdentity();
	uint32_t type = 0, flags = 0;
	bool isIncludedScene = false;

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

	//uint32_t refcount = 0;
	size_t getRefCount() { return g_objRefCounts[this]; }

	GameObject(const char *nName = "Unnamed", int nType = 0) : name(nName), type(nType) {}
	GameObject(const GameObject& other) = default;
	~GameObject() = default;

	std::string getPath() const;
	GameObject* findByPath(std::string_view path) const;
	Matrix getGlobalTransform(GameObject* reference = nullptr) const;
};

inline void GORef::deref() noexcept { if (m_obj) { g_objRefCounts[m_obj]--; m_obj = nullptr; } }
inline void GORef::set(GameObject * obj) noexcept { deref(); m_obj = obj; if (m_obj) g_objRefCounts[m_obj]++; }

struct Scene {
	Chunk oldSpkChunk;
	GameObject* rootobj = nullptr, * cliprootobj = nullptr, * superroot = nullptr;
	std::filesystem::path lastSpkFilepath;
	std::vector<uint8_t> zipmem;
	Chunk palPack, dxtPack, lgtPack, anmPack, wavPack;
	bool hasAnmPack = false;
	bool ready = false;
	AudioManager audioMgr;

	std::string zdefNames;
	DBLList zdefValues;
	std::string zdefTypes;
	std::map<uint32_t, std::pair<std::string, std::string>> msgDefinitions;

	std::vector<std::tuple<std::string, std::string, uint32_t>> textureMaterialMap;
	uint32_t numTextures = 0;
	
	std::vector<std::string> zipFilesIncluded;
	std::vector<std::string> dlcFiles;
	std::vector<std::string> scenePaths;

	std::vector<Chunk> remainingChunks; // such as PSCR

	void LoadEmpty();
	void LoadSceneSPK(const std::filesystem::path& fn);
	Chunk ConstructSPK();
	void SaveSceneSPK(const std::filesystem::path& fn);
	void Close();
	~Scene() { Close(); }
	
	GameObject* CreateObject(int type, GameObject* parent);
	void RemoveObject(GameObject *o);
	GameObject* DuplicateObject(GameObject *o, GameObject *parent = nullptr);
	void GiveObject(GameObject *o, GameObject *t);
};
extern Scene g_scene;

const char* GetObjTypeString(uint32_t ot);

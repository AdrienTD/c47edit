#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "chunk.h"

//struct Chunk;

struct AudioRef {
	uint32_t id = 0;
};

struct AudioObject {
	template <typename R>
	void reflect(R& r) {}

	static constexpr int TYPEID = 0;
	virtual ~AudioObject() = default;
	virtual int getType() const { return TYPEID; }
};

struct AudioManager {
	std::vector<std::shared_ptr<AudioObject>> audioObjects;
	std::vector<std::string> audioNames;

	void allocateSlot(size_t index);
	AudioObject* getObject(uint32_t id);
	template <typename T> T* getObjectAs(uint32_t id) {
		AudioObject* obj = getObject(id);
		if (obj && obj->getType() == T::TYPEID)
			return (T*)obj;
		return nullptr;
	}

	void load(const Chunk& ands, const Chunk& sndr);
	std::pair<Chunk, Chunk> save() const;
};

struct WaveAudioObject : AudioObject {
	static constexpr int TYPEID = 1;
	virtual int getType() const { return TYPEID; }
};

struct SoundAudioObject : AudioObject {
	AudioRef waveRef;
	float param1 = 12.7f, param2 = 1.7f, param3 = 0.0f;
	uint32_t param4 = 0;

	template <typename R>
	void reflect(R& r) {
		AudioObject::reflect(r);
		r.member(waveRef, "waveRef");
		r.member(param1, "param1");
		r.member(param2, "param2");
		r.member(param3, "param3");
		r.member(param4, "param4");
	}

	static constexpr int TYPEID = 2;
	virtual int getType() const { return TYPEID; }
};

struct SetAudioObject : AudioObject {
	float param1 = 100.0f, param2 = 1.0f;
	uint32_t param3 = 1, param4 = 0, param5 = 0, param6 = 0;
	std::vector<AudioRef> sounds;

	template <typename R>
	void reflect(R& r) {
		AudioObject::reflect(r);
		r.member(param1, "param1");
		r.member(param2, "param2");
		r.member(param3, "param3");
		r.member(param4, "param4");
		r.member(param4, "param5");
		r.member(param4, "param6");
		// sounds vector needs special treatment
	}

	static constexpr int TYPEID = 3;
	virtual int getType() const { return TYPEID; }
};

struct MaterialAudioObject : AudioObject {
	uint32_t matParam0 = 0;
	uint32_t matParam1 = 0;
	uint32_t matParam2 = 0;
	uint32_t matParam3 = 0;
	uint32_t matParam4 = 0;
	uint32_t matParam5 = 0;
	uint32_t matParam6 = 0;
	uint32_t matParam7 = 0;

	template <typename R>
	void reflect(R& r) {
		AudioObject::reflect(r);
		r.member(matParam0, "matParam0");
		r.member(matParam1, "matParam1");
		r.member(matParam2, "matParam2");
		r.member(matParam3, "matParam3");
		r.member(matParam4, "matParam4");
		r.member(matParam5, "matParam5");
		r.member(matParam6, "matParam6");
		r.member(matParam7, "matParam7");
	}

	static constexpr int TYPEID = 4;
	virtual int getType() const { return TYPEID; }
};

struct ImpactAudioObject : AudioObject {
	AudioRef set;
	AudioRef mat1, mat2;

	template <typename R>
	void reflect(R& r) {
		AudioObject::reflect(r);
		r.member(set, "set");
		r.member(mat1, "mat1");
		r.member(mat2, "mat2");
	}

	static constexpr int TYPEID = 5;
	virtual int getType() const { return TYPEID; }
};

struct RoomAudioObject : AudioObject {
	uint32_t raoUnk0;
	float raoUnk1;
	float raoUnk2;
	uint32_t raoUnk3;
	uint32_t raoUnk4;
	uint32_t raoUnk5;
	float raoUnk6;
	uint32_t raoUnk7;
	float raoUnk8;
	float raoUnk9;
	float raoUnk10;
	uint32_t raoUnk11;
	float raoUnk12;
	uint32_t raoUnk13;
	uint32_t raoUnk14;
	uint32_t raoUnk15;
	uint32_t raoUnk16;

	template <typename R>
	void reflect(R& r) {
		AudioObject::reflect(r);
		r.member(raoUnk0, "raoUnk0");
		r.member(raoUnk1, "raoUnk1");
		r.member(raoUnk2, "raoUnk2");
		r.member(raoUnk3, "raoUnk3");
		r.member(raoUnk4, "raoUnk4");
		r.member(raoUnk5, "raoUnk5");
		r.member(raoUnk6, "raoUnk6");
		r.member(raoUnk7, "raoUnk7");
		r.member(raoUnk8, "raoUnk8");
		r.member(raoUnk9, "raoUnk9");
		r.member(raoUnk10, "raoUnk10");
		r.member(raoUnk11, "raoUnk11");
		r.member(raoUnk12, "raoUnk12");
		r.member(raoUnk13, "raoUnk13");
		r.member(raoUnk14, "raoUnk14");
		r.member(raoUnk15, "raoUnk15");
		r.member(raoUnk16, "raoUnk16");
	}

	static constexpr int TYPEID = 6;
	virtual int getType() const { return TYPEID; }
};

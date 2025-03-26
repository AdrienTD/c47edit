#include "AudioManager.h"
#include <cassert>

template <typename T>
static void mdcReadTo(const Chunk::DataBuffer*& chkDataIt, T& val)
{
	static_assert(std::is_arithmetic_v<T>);
	val = *(const T*)chkDataIt->data();
	++chkDataIt;
}

template <>
void mdcReadTo(const Chunk::DataBuffer*& chkDataIt, std::string& val)
{
	val = (const char*)chkDataIt->data();
	++chkDataIt;
}

template <>
void mdcReadTo(const Chunk::DataBuffer*& chkDataIt, AudioRef& val)
{
	val.id = *(const uint32_t*)chkDataIt->data();
	++chkDataIt;
}

template <typename T>
static void mdcWrite(Chunk& chunk, const T& val)
{
	static_assert(std::is_arithmetic_v<T>);
	auto& buf = chunk.multidata.emplace_back();
	buf.resize(sizeof(T));
	memcpy(buf.data(), &val, sizeof(T));
}

template <>
void mdcWrite(Chunk& chunk, const std::string& val)
{
	auto& buf = chunk.multidata.emplace_back();
	buf.resize(val.size() + 1);
	memcpy(buf.data(), val.data(), val.size() + 1);
}

template <>
void mdcWrite(Chunk& chunk, const AudioRef& val)
{
	auto& buf = chunk.multidata.emplace_back();
	buf.resize(4);
	memcpy(buf.data(), &val.id, 4);
}

static constexpr uint32_t byteSwap32(uint32_t v) { return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | (v >> 24); };

struct RLoader {
	const Chunk::DataBuffer*& bufptr;
	RLoader(const Chunk::DataBuffer*& bufptr) : bufptr(bufptr) {}
	template<typename T> void member(T& val, const char* name) { mdcReadTo(bufptr, val); }
};

struct RSaver {
	Chunk& chunk;
	RSaver(Chunk& chunk) : chunk(chunk) {}
	template<typename T> void member(T& val, const char* name) { mdcWrite(chunk, val); }
};

template <typename T>
struct TypeIndicator {
	using type = T;
};

void AudioManager::allocateSlot(size_t index)
{
	if (index >= audioObjects.size()) {
		audioObjects.resize(index + 1);
		audioNames.resize(index + 1);
	}
	assert(index != 0);
}

AudioObject* AudioManager::getObject(uint32_t id)
{
	if (id > 0 && id < audioObjects.size())
		return audioObjects[id].get();
	return nullptr;
}

void AudioManager::load(const Chunk& ands, const Chunk& sndr)
{
	auto loadType = [&](uint32_t tag, auto what) {
		using T = typename decltype(what)::type;
		const Chunk* chk = ands.findSubchunk(byteSwap32(tag));
		assert(chk != nullptr);
		const Chunk::DataBuffer* bufptr = chk->multidata.data();
		uint32_t mostlyOne, numObjects;
		mdcReadTo(bufptr, mostlyOne);
		mdcReadTo(bufptr, numObjects);
		RLoader rl = RLoader{ bufptr };
		for (size_t i = 0; i < numObjects; ++i) {
			uint32_t id; std::string name;
			mdcReadTo(bufptr, id);
			mdcReadTo(bufptr, name);
			auto obj = std::make_shared<T>();
			obj->reflect(rl);
			if constexpr (std::is_same_v<T, SetAudioObject>) {
				SetAudioObject* set = (SetAudioObject*)obj.get();
				const Chunk::DataBuffer* setsPtr = chk->subchunks[i].multidata.empty() ? &chk->subchunks[i].maindata : chk->subchunks[i].multidata.data();
				uint32_t numEntries;
				mdcReadTo(setsPtr, numEntries);
				set->sounds.resize(numEntries);
				for (auto& entry : set->sounds)
					mdcReadTo(setsPtr, entry);
			}
			allocateSlot(id);
			audioObjects[id] = std::move(obj);
			audioNames[id] = std::move(name);
		}
	};
	loadType('WAVC', TypeIndicator<WaveAudioObject>());
	loadType('SNDC', TypeIndicator<SoundAudioObject>());
	loadType('SETC', TypeIndicator<SetAudioObject>());
	loadType('MTLS', TypeIndicator<MaterialAudioObject>());
	loadType('MMPS', TypeIndicator<ImpactAudioObject>());
	loadType('ROMS', TypeIndicator<RoomAudioObject>());

	const Chunk::DataBuffer* sndrPtr = sndr.multidata.data();
	for (size_t i = 0; i < sndr.multidata.size(); i += 2) {
		uint32_t id;
		std::string name;
		mdcReadTo(sndrPtr, id);
		mdcReadTo(sndrPtr, name);
		allocateSlot(id);
		if (audioObjects[id]) {
			assert(audioNames[id] == name);
		}
		audioNames[id] = std::move(name);
	}
}

std::pair<Chunk, Chunk> AudioManager::save() const
{
	Chunk ands;
	ands.tag = byteSwap32('ANDS');
	ands.maindata.resize(4);
	*(uint32_t*)ands.maindata.data() = 1;
	auto saveType = [&](uint32_t tag, auto what) {
		using T = typename decltype(what)::type;
		Chunk& chk = ands.subchunks.emplace_back(byteSwap32(tag));
		uint32_t mostlyOne = 1;
		mdcWrite(chk, mostlyOne);
		mdcWrite(chk, mostlyOne); // changed at the end
		RSaver rw = RSaver{ chk };
		uint32_t counter = 0;
		for (uint32_t id = 1; id < audioObjects.size(); ++id) {
			auto& obj = audioObjects[id];
			auto& name = audioNames[id];
			if (obj && obj->getType() == T::TYPEID) {
				mdcWrite(chk, (uint32_t)id);
				mdcWrite(chk, name);
				((T*)obj.get())->reflect(rw);
				if constexpr (std::is_same_v<T, SetAudioObject>) {
					const SetAudioObject* set = (const SetAudioObject*)obj.get();
					Chunk& setsChunk = chk.subchunks.emplace_back(byteSwap32('SETS'));
					uint32_t numEntries = set->sounds.size();
					mdcWrite(setsChunk, numEntries);
					for (auto& entry : set->sounds)
						mdcWrite(setsChunk, entry);
				}
				counter += 1;
			}
		}
		*(uint32_t*)chk.multidata[1].data() = counter;
	};
	saveType('WAVC', TypeIndicator<WaveAudioObject>());
	saveType('SNDC', TypeIndicator<SoundAudioObject>());
	saveType('SETC', TypeIndicator<SetAudioObject>());
	saveType('MTLS', TypeIndicator<MaterialAudioObject>());
	saveType('MMPS', TypeIndicator<ImpactAudioObject>());
	saveType('ROMS', TypeIndicator<RoomAudioObject>());

	Chunk sndr;
	sndr.tag = byteSwap32('SNDR');
	sndr.multidata.reserve(2 * audioNames.size());
	for (size_t id = 1; id < audioNames.size();  ++id) {
		auto& name = audioNames[id];
		if (!name.empty()) {
			mdcWrite(sndr, (uint32_t)id);
			mdcWrite(sndr, name);
		}
	}
	return { std::move(ands), std::move(sndr) };
}

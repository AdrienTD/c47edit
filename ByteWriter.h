#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

// Adapter for container that adds methods to append arbitrary binary data
template<typename Container> class ByteWriter {
public:
	using Byte = typename Container::value_type;
	static_assert(sizeof(Byte) == 1, "The container's element type must have the size of a byte (char, uint8_t).");

	size_t size() const { return buffer.size(); }

	void addData(const void* data, size_t length) {
		const Byte* ptr = static_cast<const Byte*>(data);
		buffer.insert(buffer.end(), ptr, ptr + length);
	}

	Byte* addEmpty(size_t length) {
		size_t oldSize = buffer.size();
		buffer.resize(oldSize + length);
		return buffer.data() + oldSize;
	}

	template<typename V> void addValue(const V& val) {
		static_assert(std::has_unique_object_representations_v<V>);
		addData(&val, sizeof(V));
	}

	void addU8(uint8_t val) { addData(&val, 1); }
	void addU16(uint16_t val) { addData(&val, 2); }
	void addU32(uint32_t val) { addData(&val, 4); }
	void addS8(int8_t val) { addData(&val, 1); }
	void addS16(int16_t val) { addData(&val, 2); }
	void addS32(int32_t val) { addData(&val, 4); }
	void addFloat(float val) { addData(&val, 4); }
	void addDouble(double val) { addData(&val, 8); }
	void addStringNT(const std::string& val) { addData(val.data(), val.size() + 1); }

	Container take() { return std::move(buffer); }
	Byte* getPointer(size_t addr = 0) { return buffer.data() + addr; }

private:
	Container buffer;
};
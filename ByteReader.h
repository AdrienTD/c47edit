#pragma once

#include <cstdint>
#include <cstring>
#include <tuple>
#include <type_traits>
#include "vecmat.h"

class ByteReader
{
public:
	ByteReader(const void* ptr) : m_ptr(static_cast<const uint8_t*>(ptr)) {}

	uint8_t readByte() { uint8_t val = *m_ptr; m_ptr += 1; return val; }
	uint16_t readUint16() { uint16_t val = *reinterpret_cast<const uint16_t*>(m_ptr); m_ptr += 2; return val; }
	uint32_t readUint32() { uint32_t val = *reinterpret_cast<const uint32_t*>(m_ptr); m_ptr += 4; return val; }
	int32_t readInt32() { int32_t val = *reinterpret_cast<const int32_t*>(m_ptr); m_ptr += 4; return val; }
	float readFloat() { float val = *reinterpret_cast<const float*>(m_ptr); m_ptr += 4; return val; }

	template<typename T>
	void readTo(T& val) {
		static_assert(std::is_arithmetic_v<T>, "cannot read to this type, only integers and floats");
		val = *reinterpret_cast<std::add_const_t<T>*>(m_ptr);
		m_ptr += sizeof(T);
	}
	template<>
	void readTo<Vector3>(Vector3& val) {
		readTo(val.x);
		readTo(val.y);
		readTo(val.z);
	}

	template<typename T, typename ... Rest>
	void readTo(T& val, Rest& ... rest) {
		readTo(val);
		(readTo(rest), ...);
	}

	template<typename ... Types>
	std::tuple<Types...> readTuple() {
		std::tuple<Types...> tup;
		std::apply([](auto& ... elem) { (readTo(elem), ...); }, tup);
		return tup;
	}

	void readToBuffer(void* data, size_t length) {
		std::memcpy(data, m_ptr, length);
		m_ptr += length;
	}
	void skip(size_t offset) { m_ptr += offset; }

	const uint8_t* currentPointer() const { return m_ptr; }

private:
	const uint8_t* m_ptr;
};

// c47edit - Scene editor for HM C47
// Copyright (C) 2018-2022 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <cstddef>
#include <cstring>

template <class T> class DynArray {
private:
	T* pointer;
	size_t length;

	void freeP() {
		if (length)
			delete[] pointer;
		pointer = nullptr;
		length = 0;
	}

	void setsize(size_t newlen) {
		if (newlen)
			pointer = new T[newlen];
		length = newlen;
	}

public:
	void resize(size_t newlen) {
		freeP();
		setsize(newlen);
	}

	size_t size() const { return length; }
	T* data() { return pointer; }
	const T* data() const { return pointer; }

	T* begin() { return pointer; }
	T* end() { return pointer + length; }
	const T* begin() const { return pointer; }
	const T* end() const { return pointer + length; }

	T& operator[] (size_t index) { return pointer[index]; }
	const T& operator[] (size_t index) const { return pointer[index]; }

	DynArray() { pointer = nullptr; length = 0; }
	DynArray(int len) { setsize(len); }
	DynArray(const DynArray &other) { setsize(other.length); memcpy(pointer, other.pointer, length * sizeof(T)); }
	DynArray(DynArray &&other) noexcept { pointer = other.pointer; length = other.length; other.pointer = nullptr; other.length = 0; }
	void operator=(const DynArray &other) { resize(other.length); memcpy(pointer, other.pointer, length * sizeof(T)); }
	void operator=(DynArray &&other) noexcept { freeP(); pointer = other.pointer; length = other.length; other.pointer = nullptr; other.length = 0; }
	~DynArray() { freeP(); }
};

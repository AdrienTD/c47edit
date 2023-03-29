// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "DynArray.h"

struct Chunk
{
	using DataBuffer = DynArray<uint8_t>;

	uint32_t tag = 0;
	std::vector<DataBuffer> multidata;
	std::vector<Chunk> subchunks;
	DataBuffer maindata;

	Chunk() = default;
	Chunk(uint32_t tag) : tag(tag) {};
	~Chunk();

	const Chunk* findSubchunk(uint32_t tag) const;
	Chunk* findSubchunk(uint32_t tag) { return (Chunk*)std::as_const(*this).findSubchunk(tag); }

	void load(void *bytes);
	std::string saveToString();
	static Chunk reconstructPackFromRepeat(void *packrep, uint32_t packrepsize, void *repeat);
};

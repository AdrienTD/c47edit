// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <cstdint>
#include <vector>
#include "DynArray.h"

struct Chunk
{
	using DataBuffer = DynArray<uint8_t>;

	uint32_t tag = 0;
	std::vector<DataBuffer> multidata;
	std::vector<Chunk> subchunks;
	DataBuffer maindata;

	~Chunk();

	Chunk *findSubchunk(uint32_t tag);

	void load(void *bytes);
	void saveToMem(void **pnt, size_t *size);
	static Chunk reconstructPackFromRepeat(void *packrep, uint32_t packrepsize, void *repeat);
};

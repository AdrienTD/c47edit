// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <cstdint>

struct Chunk
{
	uint32_t tag;
	uint32_t size;
	//boolean has_multidata, has_subchunks;
	uint32_t num_datas, num_subchunks;
	void **multidata; uint32_t* multidata_sizes;
	Chunk *subchunks;
	void *maindata; uint32_t maindata_size;

	~Chunk();

	Chunk *findSubchunk(uint32_t tag);
};

void LoadChunk(Chunk *chk, void *bytes);
void SaveChunkToMem(Chunk *chk, void **pnt, size_t *size);
Chunk *ReconstructPackFromRepeat(void *packrep, uint32_t packrepsize, void *repeat);

// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

struct Chunk
{
	uint tag;
	uint size;
	//boolean has_multidata, has_subchunks;
	uint num_datas, num_subchunks;
	void **multidata; uint *multidata_sizes;
	Chunk *subchunks;
	void *maindata; uint maindata_size;

	Chunk *findSubchunk(uint tag);
};

void LoadChunk(Chunk *chk, void *bytes);
void SaveChunkToMem(Chunk *chk, void **pnt, size_t *size);

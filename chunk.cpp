// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "chunk.h"
#include <sstream>
#include <functional>
#include <map>

Chunk::~Chunk()
{
	for (uint32_t i = 0; i < num_datas; ++i)
		free(multidata[i]);
	if (num_datas) {
		delete[] multidata;
		delete[] multidata_sizes;
	}
	free(maindata);
	if (num_subchunks)
		delete[] subchunks;
}

Chunk *Chunk::findSubchunk(uint32_t tagkey)
{
	for (int i = 0; i < this->num_subchunks; i++)
		if (this->subchunks[i].tag == tagkey)
			return &this->subchunks[i];
	return 0;
}

void LoadChunk(Chunk *chk, void *bytes)
{
	uint32_t* pnt = (uint32_t*)bytes;

	chk->tag = *(pnt++);
	uint32_t it = *(pnt++);
	chk->size = it & 0x3FFFFFFF;
	bool has_subchunks = it & 0x80000000;
	bool has_multidata = it & 0x40000000;

	uint32_t odat;
	if(has_subchunks || has_multidata)
		odat = *(pnt++);
	else
		odat = 8;

	if(has_subchunks) chk->num_subchunks = *(pnt++);
	else chk->num_subchunks = 0;

	if(has_multidata)
	{
		chk->num_datas = *(pnt++);
		chk->multidata = new void*[chk->num_datas];
		chk->multidata_sizes = new uint32_t[chk->num_datas];
		for(int i = 0; i < chk->num_datas; i++)
		{
			chk->multidata_sizes[i] = *(pnt++);
			chk->multidata[i] = malloc(chk->multidata_sizes[i]);
		}
	}
	else	chk->num_datas = 0;

	// Subchunks
	chk->subchunks = new Chunk[chk->num_subchunks];
	for(int i = 0; i < chk->num_subchunks; i++)
	{
		LoadChunk(&chk->subchunks[i], pnt);
		pnt = (uint32_t*)( (char*)pnt + chk->subchunks[i].size );
	}

	// Data / Multidata
	char *dp = (char*)bytes + odat;
	if(has_multidata)
	{
		for(int i = 0; i < chk->num_datas; i++)
		{
			uint32_t siz = chk->multidata_sizes[i];
			memcpy(chk->multidata[i], dp, siz);
			dp += siz;
		}
		chk->maindata = 0; chk->maindata_size = 0;
	}
	else
	{
		chk->maindata_size = chk->size - odat;
		chk->maindata = malloc(chk->maindata_size);
		memcpy(chk->maindata, dp, chk->maindata_size);
	}
}

uint32_t sbtell(std::stringbuf *sb)
{
	return sb->pubseekoff(0, std::ios_base::cur, std::ios_base::out);
}

void WriteChunkToStringBuf(std::stringbuf *sb, Chunk *chk)
{
	// Header
	uint32_t begoff = sbtell(sb);
	sb->sputn((char*)&(chk->tag), 4);
	bool hasmultidata = chk->num_datas > 0;
	bool hassubchunks = chk->num_subchunks > 0;
	//int size = 8 + chk->maindata_size;
	int iv = 0;
	sb->sputn((char*)&iv, 4);
	if (hasmultidata || hassubchunks)
		sb->sputn((char*)&iv, 4);
	if (hassubchunks)
		sb->sputn((char*)&(chk->num_subchunks), 4);
	if (hasmultidata)
	{
		sb->sputn((char*)&(chk->num_datas), 4);
		for(int i = 0; i < chk->num_datas; i++)
			sb->sputn((char*)&(chk->multidata_sizes[i]), 4);
	}

	// Subchunks
	if (hassubchunks)
		for (int i = 0; i < chk->num_subchunks; i++)
			WriteChunkToStringBuf(sb, &(chk->subchunks[i]));

	// Data / Multidata
	uint32_t odat = sbtell(sb) - begoff;
	if (hasmultidata)
		for (int i = 0; i < chk->num_datas; i++)
			sb->sputn((char*)chk->multidata[i], chk->multidata_sizes[i]);
	else
		sb->sputn((char*)chk->maindata, chk->maindata_size);
	uint32_t endoff = sbtell(sb);

	sb->pubseekpos(begoff + 4, std::ios_base::out);
	uint32_t lenflags = (endoff - begoff) | (hasmultidata ? 0x40000000 : 0) | (hassubchunks ? 0x80000000 : 0);
	sb->sputn((char*)&lenflags, 4);
	if (hasmultidata || hassubchunks)
		sb->sputn((char*)&odat, 4);
	sb->pubseekpos(endoff);
}

void SaveChunkToMem(Chunk *chk, void **pnt, size_t *size)
{
	std::stringbuf sb;
	WriteChunkToStringBuf(&sb, chk);
	std::string m = sb.str();
	/*
	FILE *f;
	assert(f = fopen("out.spk", "wb"));
	fwrite(m.data(), m.length(), 1, f);
	fclose(f);
	*/
	size_t l = m.length();
	*pnt = malloc(l);
	*size = l;
	memcpy(*pnt, m.data(), l);
}

Chunk *ReconstructPackFromRepeat(void *packrep, uint32_t packrepsize, void *repeat)
{
	Chunk *mainchk = new Chunk;

	uint32_t *ppnt = (uint32_t*)packrep;
	uint32_t reconsoff = *(ppnt + 1);
	ppnt += 2;

	uint32_t currp = 0;
	std::map<uint32_t, Chunk*> rpmap;

	std::function<void(Chunk*)> f;
	f = [&f, &ppnt, &rpmap, &currp](Chunk *c)
	{
		uint32_t beg = currp;
		c->num_datas = 0; c->num_subchunks = 0;
		c->tag = *(ppnt++);
		uint32_t info = *(ppnt++);
		uint32_t csize = info & 0x3FFFFFFF;
		uint32_t datoff = 8;
		if (info & 0xC0000000)
			datoff = *(ppnt++);

		rpmap[currp + datoff] = c;

		if (info & 0x80000000)
		{
			c->num_subchunks = *(ppnt++);
			c->subchunks = new Chunk[c->num_subchunks];
			currp += 16;
			for (int i = 0; i < c->num_subchunks; i++)
				f(&c->subchunks[i]);
		}

		currp = beg + csize;
	};

	f(mainchk);

	char* reconspnt = (char*)packrep + 8 + reconsoff;
	ppnt = (uint32_t*)reconspnt;
	uint32_t reconssize = packrepsize - reconsoff;
	while ((char*)ppnt - reconspnt <= reconssize - 16)
	{
		uint32_t repeatoff = *(ppnt++);
		Chunk *c = rpmap[*(ppnt++)];
		c->maindata_size = *(ppnt++);
		c->maindata = malloc(c->maindata_size);
		memcpy(c->maindata, (char*)repeat + repeatoff, c->maindata_size);
		*(uint32_t*)c->maindata = *(ppnt++);
	}

	return mainchk;
}

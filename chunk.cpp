// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "global.h"
#include <sstream>

Chunk *Chunk::findSubchunk(uint tagkey)
{
	for (int i = 0; i < this->num_subchunks; i++)
		if (this->subchunks[i].tag == tagkey)
			return &this->subchunks[i];
	return 0;
}

void LoadChunk(Chunk *chk, void *bytes)
{
	uint* pnt = (uint*)bytes;

	chk->tag = *(pnt++);
	uint it = *(pnt++);
	chk->size = it & 0x3FFFFFFF;
	bool has_subchunks = it & 0x80000000;
	bool has_multidata = it & 0x40000000;

	uint odat;
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
		chk->multidata_sizes = new uint[chk->num_datas];
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
		pnt = (uint*)( (char*)pnt + chk->subchunks[i].size );
	}

	// Data / Multidata
	char *dp = (char*)bytes + odat;
	if(has_multidata)
	{
		for(int i = 0; i < chk->num_datas; i++)
		{
			uint siz = chk->multidata_sizes[i];
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

uint sbtell(std::stringbuf *sb)
{
	return sb->pubseekoff(0, std::ios_base::cur, std::ios_base::out);
}

void WriteChunkToStringBuf(std::stringbuf *sb, Chunk *chk)
{
	// Header
	uint begoff = sbtell(sb);
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
	uint odat = sbtell(sb) - begoff;
	if (hasmultidata)
		for (int i = 0; i < chk->num_datas; i++)
			sb->sputn((char*)chk->multidata[i], chk->multidata_sizes[i]);
	else
		sb->sputn((char*)chk->maindata, chk->maindata_size);
	uint endoff = sbtell(sb);

	sb->pubseekpos(begoff + 4, std::ios_base::out);
	uint lenflags = (endoff - begoff) | (hasmultidata ? 0x40000000 : 0) | (hassubchunks ? 0x80000000 : 0);
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
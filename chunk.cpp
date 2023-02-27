// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "chunk.h"
#include <sstream>
#include <map>
#include <cassert>

Chunk::~Chunk() = default;

Chunk *Chunk::findSubchunk(uint32_t tagkey)
{
	for (Chunk& sub : subchunks)
		if (sub.tag == tagkey)
			return &sub;
	return nullptr;
}

void Chunk::load(void *bytes)
{
	Chunk *chk = this;

	uint32_t* pnt = (uint32_t*)bytes;

	chk->tag = *(pnt++);
	uint32_t it = *(pnt++);
	uint32_t chksize = it & 0x3FFFFFFF;
	bool has_subchunks = it & 0x80000000;
	bool has_multidata = it & 0x40000000;

	uint32_t odat;
	if(has_subchunks || has_multidata)
		odat = *(pnt++);
	else
		odat = 8;

	uint32_t num_subchunks;
	if(has_subchunks) num_subchunks = *(pnt++);
	else num_subchunks = 0;

	if(has_multidata)
	{
		uint32_t num_datas = *(pnt++);
		chk->multidata.resize(num_datas);
		for(auto& dat : chk->multidata)
		{
			uint32_t datlen = *(pnt++);
			dat.resize(datlen);
		}
	}
	else	chk->multidata.clear();

	// Subchunks
	chk->subchunks.resize(num_subchunks);
	for(auto& subchunk : subchunks)
	{
		subchunk.load(pnt);
		uint32_t sublen = pnt[1] & 0x3FFFFFFF;
		pnt = (uint32_t*)( (char*)pnt + sublen );
	}

	// Data / Multidata
	char *dp = (char*)bytes + odat;
	if(has_multidata)
	{
		for(auto& dat : multidata)
		{
			memcpy(dat.data(), dp, dat.size());
			dp += dat.size();
		}
		// clear maindata
	}
	else
	{
		uint32_t mainlen = chksize - odat;
		maindata.resize(mainlen);
		memcpy(maindata.data(), dp, maindata.size());
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
	bool hasmultidata = !chk->multidata.empty();
	bool hassubchunks = !chk->subchunks.empty();
	//int size = 8 + chk->maindata_size;
	int iv = 0;
	sb->sputn((char*)&iv, 4);
	if (hasmultidata || hassubchunks)
		sb->sputn((char*)&iv, 4);
	if (hassubchunks) {
		uint32_t num_subchunks = (uint32_t)chk->subchunks.size();
		sb->sputn((char*)&num_subchunks, 4);
	}
	if (hasmultidata)
	{
		uint32_t num_datas = (uint32_t)chk->multidata.size();
		sb->sputn((char*)&num_datas, 4);
		for (auto& dat : chk->multidata) {
			uint32_t len = (uint32_t)dat.size();
			sb->sputn((char*)&len, 4);
		}
	}

	// Subchunks
	if (hassubchunks)
		for (auto& subchunk : chk->subchunks)
			WriteChunkToStringBuf(sb, &subchunk);

	// Data / Multidata
	uint32_t odat = sbtell(sb) - begoff;
	if (hasmultidata)
		for (auto& dat : chk->multidata)
			sb->sputn((char*)dat.data(), dat.size());
	else
		sb->sputn((char*)chk->maindata.data(), chk->maindata.size());
	uint32_t endoff = sbtell(sb);

	sb->pubseekpos(begoff + 4, std::ios_base::out);
	uint32_t lenflags = (endoff - begoff) | (hasmultidata ? 0x40000000 : 0) | (hassubchunks ? 0x80000000 : 0);
	sb->sputn((char*)&lenflags, 4);
	if (hasmultidata || hassubchunks)
		sb->sputn((char*)&odat, 4);
	sb->pubseekpos(endoff);
}

void Chunk::saveToMem(void **pnt, size_t *size)
{
	std::stringbuf sb;
	WriteChunkToStringBuf(&sb, this);
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

Chunk Chunk::reconstructPackFromRepeat(void *packrep, uint32_t packrepsize, void *repeat)
{
	Chunk mainchk;

	uint32_t *ppnt = (uint32_t*)packrep;
	uint32_t reconsoff = *(ppnt + 1);
	ppnt += 2;

	uint32_t currp = 0;
	std::map<uint32_t, std::pair<Chunk*, int>> rpmap;

	auto f = [&ppnt, &rpmap, &currp](Chunk *c, const auto& rec) -> void {
		uint32_t beg = currp;
		c->tag = *(ppnt++);
		uint32_t info = *(ppnt++);
		uint32_t csize = info & 0x3FFFFFFF;
		bool has_subchunks = info & 0x80000000;
		bool has_multidata = info & 0x40000000;
		uint32_t datoff = 8;
		if (has_subchunks || has_multidata)
			datoff = *(ppnt++);

		uint32_t num_subchunks = 0;
		if (has_subchunks) {
			num_subchunks = *(ppnt++);
			c->subchunks.resize(num_subchunks);
		}

		uint32_t num_multidata = 0;
		if (has_multidata) {
			num_multidata = *(ppnt++);
			c->multidata.resize(num_multidata);
			for (auto& dat : c->multidata) {
				uint32_t datlen = *(ppnt++);
				dat.resize(datlen);
			}
		}

		if (has_multidata) {
			for (int md = 0; md < num_multidata; ++md) {
				rpmap[currp + datoff] = { c, md };
				datoff += c->multidata[md].size();
			}
		}
		else {
			rpmap[currp + datoff] = { c, -1 };
		}

		if (has_subchunks) {
			currp += 16 + (has_multidata ? 4 + num_multidata*4 : 0);
			for (int i = 0; i < num_subchunks; i++)
				rec(&c->subchunks[i], rec);
		}

		currp = beg + csize;
	};

	f(&mainchk, f);

	char* reconspnt = (char*)packrep + 8 + reconsoff;
	ppnt = (uint32_t*)reconspnt;
	uint32_t reconssize = packrepsize - (8 + reconsoff);
	while ((char*)ppnt - reconspnt < reconssize)
	{
		uint32_t repeatoff = *(ppnt++);
		Chunk* c; int multiDataIndex;
		std::tie(c, multiDataIndex) = rpmap.at(*(ppnt++));
		uint32_t data_size = *(ppnt++);
		DataBuffer& dataBuf = (multiDataIndex == -1) ? c->maindata : c->multidata[multiDataIndex];
		assert(multiDataIndex == -1 || dataBuf.size() == data_size);
		dataBuf.resize(data_size);
		memcpy(dataBuf.data(), (char*)repeat + repeatoff, dataBuf.size());
		*(uint32_t*)dataBuf.data() = *(ppnt++);
	}

	return mainchk;
}

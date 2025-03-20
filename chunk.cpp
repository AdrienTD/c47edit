// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "chunk.h"
#include <map>
#include <cassert>
#include <cstring>
#include "ByteWriter.h"

Chunk::~Chunk() = default;

const Chunk *Chunk::findSubchunk(uint32_t tagkey) const
{
	for (const Chunk& sub : subchunks)
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

static void WriteChunkToStringBuf(ByteWriter<std::string>& sb, Chunk *chk)
{
	// Header
	uint32_t begoff = (uint32_t)sb.size();
	sb.addU32(chk->tag);
	bool hasmultidata = !chk->multidata.empty();
	bool hassubchunks = !chk->subchunks.empty();
	sb.addS32(0); // reserved for size
	if (hasmultidata || hassubchunks)
		sb.addS32(0); // reserved for data offset
	if (hassubchunks) {
		uint32_t num_subchunks = (uint32_t)chk->subchunks.size();
		sb.addU32(num_subchunks);
	}
	if (hasmultidata)
	{
		uint32_t num_datas = (uint32_t)chk->multidata.size();
		sb.addU32(num_datas);
		for (auto& dat : chk->multidata) {
			uint32_t len = (uint32_t)dat.size();
			sb.addU32(len);
		}
	}

	// Subchunks
	if (hassubchunks)
		for (auto& subchunk : chk->subchunks)
			WriteChunkToStringBuf(sb, &subchunk);

	// Data / Multidata
	uint32_t odat = (uint32_t)sb.size() - begoff;
	if (hasmultidata)
		for (auto& dat : chk->multidata)
			sb.addData(dat.data(), dat.size());
	else
		sb.addData(chk->maindata.data(), chk->maindata.size());
	uint32_t endoff = (uint32_t)sb.size();

	// Write to the reserved values
	uint8_t* headerPtr = (uint8_t*)sb.getPointer(begoff);
	uint32_t lenflags = (endoff - begoff) | (hasmultidata ? 0x40000000 : 0) | (hassubchunks ? 0x80000000 : 0);
	*(uint32_t*)(headerPtr + 4) = lenflags;
	if (hasmultidata || hassubchunks)
		*(uint32_t*)(headerPtr + 8) = odat;
}

std::string Chunk::saveToString()
{
	ByteWriter<std::string> sb;
	WriteChunkToStringBuf(sb, this);
	return sb.take();
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
			for (int md = 0; md < (int)num_multidata; ++md) {
				rpmap[currp + datoff] = { c, md };
				datoff += c->multidata[md].size();
			}
		}
		else {
			rpmap[currp + datoff] = { c, -1 };
		}

		if (has_subchunks) {
			currp += 16 + (has_multidata ? 4 + num_multidata*4 : 0);
			for (auto& subchunk : c->subchunks)
				rec(&subchunk, rec);
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

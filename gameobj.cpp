// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include <functional>
#include <sstream>
#include <array>
#include <map>
#include <unordered_map>

#include "global.h"
#include "gameobj.h"
#include "chunk.h"
#include "vecmat.h"

#include "miniz.h"

const char *objtypenames[] = {
	// 0x00
	"Z0", "ZGROUP", "ZSTDOBJ", "ZCAMERA",
	"?", "?", "?", "?",
	"?", "?", "?", "Z2DOBJ",
	"?", "ZENVIRONMENT", "?", "?",

	// 0x10
	"?", "?", "ZSNDOBJ", "?",
	"?", "?", "?", "?",
	"?", "?", "ZLIST", "? ",
	"?", "?", "?", "?",

	// 0x20
	"?", "ZROOM", "?", "ZSPOTLIGHT", 
	"?", "?", "?", "ZIKLNKOBJ",
	"?", "?", "?", "ZEDITORGROUP",
	"ZWINOBJ", "ZCHAROBJ", "ZWINGROUP", "ZFONT",

	// 0x30
	"ZWINDOWS", "ZWINDOW", "?", "ZBUTTON",
	"?", "?", "?", "?",
	"ZLINEOBJ", "?", "ZTTFONT", "ZSCROLLAREA",
	"?", "?", "?", "?",

	// 0x40
	"ZSCROLLBAR", "ZSCALESTDOBJ", "?", "?",
	"?", "?", "?", "?",
	"?", "?", "?", "?",
	"?", "?", "?", "?",

	// 0x50
	"?", "?", "?", "?",
	"?", "?", "?", "?",
	"?", "?", "?", "?",
	"?", "?", "?", "?",

	// 0x60
	"?", "?", "?", "?",
	"?", "?", "ZITEMGROUP", "?",
	"?", "ZITEMGROUPWEAPON", "ZITEMGROUPAMMO", "?",
	"?", "?", "?", "?",
};

Chunk *spkchk;
Chunk *prot, *pclp, *phea, *pnam, *ppos, *pmtx, *pver, *pfac, *pftx, *puvc, *pdbl;
GameObject *rootobj, *cliprootobj, *superroot;
std::string lastspkfn;
void *zipmem = 0; uint32_t zipsize = 0;

const char *GetObjTypeString(uint32_t ot)
{
	const char *otname = "?";
	if (ot < std::size(objtypenames)) otname = objtypenames[ot];
	return otname;
}

void LoadSceneSPK(const char *fn)
{
	FILE *zipfile = fopen(fn, "rb");
	if (!zipfile) ferr("Could not open the ZIP file.");
	fseek(zipfile, 0, SEEK_END);
	zipsize = ftell(zipfile);
	fseek(zipfile, 0, SEEK_SET);
	zipmem = malloc(zipsize);
	if (!zipmem) ferr("Could not allocate memory to load the ZIP file.");
	fread(zipmem, zipsize, 1, zipfile);
	fclose(zipfile);

	mz_zip_archive zip; void *spkmem; size_t spksize;
	spkchk = new Chunk;
	mz_zip_zero_struct(&zip);
	mz_bool mzreadok = mz_zip_reader_init_mem(&zip, zipmem, zipsize, 0);
	if (!mzreadok) ferr("Failed to initialize ZIP reading.");
	spkmem = mz_zip_reader_extract_file_to_heap(&zip, "Pack.SPK", &spksize, 0);
	if (!spkmem) ferr("Failed to extract Pack.SPK from ZIP archive.");
	mz_zip_reader_end(&zip);
	spkchk->load(spkmem);
	free(spkmem);
	lastspkfn = fn;

	prot = spkchk->findSubchunk('TORP');
	pclp = spkchk->findSubchunk('PLCP');
	phea = spkchk->findSubchunk('AEHP');
	pnam = spkchk->findSubchunk('MANP');
	ppos = spkchk->findSubchunk('SOPP');
	pmtx = spkchk->findSubchunk('XTMP');
	pver = spkchk->findSubchunk('REVP');
	pfac = spkchk->findSubchunk('CAFP');
	pftx = spkchk->findSubchunk('XTFP');
	puvc = spkchk->findSubchunk('CVUP');
	pdbl = spkchk->findSubchunk('LBDP');
	if (!(prot && pclp && phea && pnam && ppos && pmtx && pver && pfac && pftx && puvc && pdbl))
		ferr("One or more important chunks were not found in Pack.SPK .");

	rootobj = new GameObject("Root", 0x21 /*ZROOM*/);
	cliprootobj = new GameObject("ClipRoot", 0x21 /*ZROOM*/);
	superroot = new GameObject("SuperRoot", 0x21);
	superroot->subobj.push_back(rootobj);
	superroot->subobj.push_back(cliprootobj);
	rootobj->parent = cliprootobj->parent = superroot;
	rootobj->root = rootobj;
	cliprootobj->root = cliprootobj;

	// First, create the objects and an ID<->GameObject* map.
	std::map<uint32_t, GameObject*> idobjmap;
	std::map<Chunk*, GameObject*> chkobjmap;
	std::function<void(Chunk*,GameObject*)> z;
	uint32_t objid = 1;
	z = [&z, &objid, &chkobjmap, &idobjmap](Chunk *c, GameObject *parentobj) {
		uint32_t pheaoff = c->tag & 0xFFFFFF;
		uint32_t *p = (uint32_t*)((char*)phea->maindata.data() + pheaoff);
		uint32_t ot = *(unsigned short*)(&p[5]);
		char *objname = (char*)pnam->maindata.data() + p[2];

		GameObject *o = new GameObject(objname, ot);
		chkobjmap[c] = o;
		parentobj->subobj.push_back(o);
		o->parent = parentobj;
		idobjmap[objid++] = o;
		if (c->subchunks.size() > 0)
			for (uint32_t i = 0; i < (uint32_t)c->subchunks.size(); i++)
				z(&c->subchunks[i], o);
	};

	auto y = [z](Chunk *c, GameObject *o) {
		for (uint32_t i = 0; i < (uint32_t)c->subchunks.size(); i++)
			z(&c->subchunks[i], o);
	};

	y(pclp, cliprootobj);
	y(prot, rootobj);

	// Then read/load the object properties.
	std::function<void(Chunk*, GameObject*)> g;
	g = [&g,&objid,&chkobjmap,&idobjmap](Chunk *c, GameObject *parentobj) {
		uint32_t pheaoff = c->tag & 0xFFFFFF;
		uint32_t *p = (uint32_t*)((char*)phea->maindata.data() + pheaoff);
		uint32_t ot = *(unsigned short*)(&p[5]);
		const char *otname = GetObjTypeString(ot);
		char *objname = (char*)pnam->maindata.data() + p[2];

		GameObject *o = chkobjmap[c];
		o->state = (c->tag >> 24) & 255;
		o->pdbloff = p[0];
		o->pexcoff = p[1];
		o->flags = *((unsigned short*)(&p[5]) + 1);
		o->root = o->parent->root;

		o->position = *(Vector3*)((char*)ppos->maindata.data() + p[4]);
		o->matrix = Matrix::getIdentity();
		float mc[4];
		int32_t *mtxoff  = (int32_t*)pmtx->maindata.data() + p[3] * 4;
		for (int i = 0; i < 4; i++)
			mc[i] = (float)((double)mtxoff[i] / 1073741824.0); // divide by 2^30
		Vector3 rv[3];
		rv[2] = Vector3(mc[0], mc[1], 1 - mc[0]*mc[0] - mc[1]*mc[1]);
		rv[1] = Vector3(mc[2], mc[3], 1 - mc[2]*mc[2] - mc[3]*mc[3]);
		if (mtxoff[0] & 1) rv[2].z = -rv[2].z;
		if (mtxoff[2] & 1) rv[1].z = -rv[1].z;
		rv[0] = rv[1].cross(rv[2]);
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				o->matrix.m[i][j] = rv[i].coord[j];

		p += 6;
		if (o->flags & 0x0020)
		{
			o->mesh = std::make_shared<Mesh>();
			Mesh *m = o->mesh.get();
			uint32_t vertstart = p[0];
			uint32_t quadstart = p[1];
			uint32_t tristart = p[2];
			uint32_t ftxOffset = p[3];
			uint32_t numverts = p[4];
			uint32_t numquads = p[5];
			uint32_t numtris = p[6];
			o->color = p[7];
			uint32_t weird = p[8];
			Vector3* chkVertices = (Vector3*)((float*)pver->maindata.data() + vertstart);
			uint16_t* chkFaces = (uint16_t*)pfac->maindata.data();
			float* chkUvs = (float*)puvc->maindata.data();
			m->vertices.assign(chkVertices, chkVertices + numverts);
			m->triIndices.assign(chkFaces + tristart, chkFaces + tristart + numtris * 3);
			m->quadIndices.assign(chkFaces + quadstart, chkFaces + quadstart + numquads * 4);
			//assert(!(ftxOffset & 0x80000000));
			if (ftxOffset & 0x80000000)
				printf("Odd FTX offset: %08X\n", ftxOffset);
			if (ftxOffset && !(ftxOffset & 0x80000000)) {
				uint8_t* ftxpnt = (uint8_t*)pftx->maindata.data() + ftxOffset - 1;
				uint16_t* ftxFace = (uint16_t*)(ftxpnt + 12);
				uint32_t uvs1start = *(uint32_t*)ftxpnt;
				uint32_t uvs2start = *(uint32_t*)(ftxpnt + 4);
				uint32_t numFaces = *(uint32_t*)(ftxpnt + 8);
				assert(numFaces == numtris + numquads);
				m->ftxFaces.resize(numFaces);
				for (auto& face : m->ftxFaces) {
					face = { ftxFace[0], ftxFace[1], ftxFace[2], ftxFace[3], ftxFace[4], ftxFace[5] };
					ftxFace += 6;
				}
				m->primaryUvs.assign(chkUvs + uvs1start, chkUvs + uvs1start + 8 * numFaces);
				m->secondaryUvs.assign(chkUvs + uvs2start, chkUvs + uvs2start + 8 * numFaces);
			}
			//if(numquads) printf("(%i, %i)\n", quadstart, quadstart + numquads * 4);
			//if (numtris) printf("(%i, %i)\n", tristart, tristart + numtris * 3);
		}
		if (o->flags & 0x0400)
		{
			o->shape = std::make_shared<Shape>();
			uint32_t vertstart = p[0];
			uint32_t quadstart = p[1];
			uint32_t tristart = p[2];
			uint32_t ftxOffset = p[3];
			uint32_t numverts = p[4];
			uint32_t numquads = p[5];
			uint32_t numtris = p[6];
			o->color = p[7];
			uint32_t weird = p[8];
			assert(quadstart == 0);
			assert(numquads == 0);

			Shape* s = o->shape.get();
			Vector3* chkVertices = (Vector3*)((float*)pver->maindata.data() + vertstart);
			s->vertices.assign(chkVertices, chkVertices + numverts);
			s->quadstart = quadstart;
			s->tristart = tristart;
			s->ftxo = ftxOffset;
			s->numquads = numquads;
			s->numtris = numtris;
			s->weird = weird;
		}

		if (o->flags & 0x0080)
		{
			Light *l = o->light = new Light;
			for (int i = 0; i < 7; i++)
				l->param[i] = p[i];
		}

		char *dpbeg = (char*)pdbl->maindata.data() + o->pdbloff;
		uint32_t ds = *(uint32_t*)dpbeg & 0xFFFFFF;
		o->dblflags = (*(uint32_t*)dpbeg >> 24) & 255;
		char *dp = dpbeg + 4;
		while (dp - dpbeg < ds)
		{
			DBLEntry &e = o->dbl.emplace_back();
			e.type = *dp & 0x3F;
			e.flags = *dp & 0xC0;
			dp++;
			switch (e.type)
			{
			case 0:
				break;
			case 1:
				e.value = *(double*)dp;
				dp += 8;
				break;
			case 2:
				e.value = *(float*)dp;
				dp += 4;
				break;
			case 3:
			case 0xA:
			case 0xB:
			case 0xC:
				e.value = *(uint32_t*)dp;
				dp += 4;
				break;
			case 4:
			case 5:
				e.value.emplace<std::string>(dp);
				while (*(dp++));
				break;
			case 6:
				break;
			case 7: {
				auto datsize = *(uint32_t*)dp - 4;
				e.value.emplace<std::vector<uint8_t>>(dp + 4, dp + 4 + datsize);
				dp += *(uint32_t*)dp;
				break;
			}
			case 8:
				e.value.emplace<GORef>(idobjmap[*(uint32_t*)dp]);
				dp += 4;
				break;
			case 9: {
				uint32_t nobjs = (*(uint32_t*)dp - 4) / 4;
				std::vector<GORef>& objlist = e.value.emplace<std::vector<GORef>>();
				for (uint32_t i = 0; i < nobjs; i++)
					objlist.emplace_back(idobjmap[*(uint32_t*)(dp+4+4*i)]);
				dp += *(uint32_t*)dp;
				break;
			}
			case 0x3F:
				break;
			default:
				ferr("Unknown DBL entry type!");
			}
		}

		if (c->subchunks.size() > 0)
		{
			for (uint32_t i = 0; i < (uint32_t)c->subchunks.size(); i++)
				g(&c->subchunks[i], o);
		}
	};

	auto f = [g](Chunk *c, GameObject *o) {
		for (uint32_t i = 0; i < (uint32_t)c->subchunks.size(); i++)
			g(&c->subchunks[i], o);
	};

	f(pclp, cliprootobj);
	f(prot, rootobj);
}

struct DBLHash
{
	size_t operator() (const std::pair<uint32_t, std::string>& e) const
	{
		const std::hash<std::string> h;
		return e.first + h(e.second);
	}
};

uint32_t checksum(void* data, size_t length) {
	uint32_t sum = 0;
	uint8_t* bytes = (uint8_t*)data;
	for (size_t i = 0; i < length; ++i)
		sum += bytes[i];
	return sum;
}

std::stringbuf heabuf, nambuf, dblbuf;
std::vector<std::array<float,3> > posbuf;
std::vector<std::array<uint32_t, 4> > mtxbuf;
std::vector<Vector3> g_sav_verbuf;
std::vector<uint16_t> g_sav_facbuf;
std::vector<float> g_sav_uvcbuf;
std::stringbuf g_sav_ftxbuf;

uint32_t sbtell(std::stringbuf *sb);

uint32_t moc_objcount;
std::map<GameObject*, uint32_t> objidmap;

std::map<std::array<float, 3>, uint32_t> g_sav_posmap;
std::map<std::array<uint32_t, 4>, uint32_t> g_sav_mtxmap;
std::map<std::string, uint32_t> g_sav_nammap;
std::unordered_map<std::pair<uint32_t, std::string>, uint32_t, DBLHash> g_sav_dblmap;
std::map<std::vector<Vector3>, uint32_t> g_sav_vermap;
std::map<std::vector<uint16_t>, uint32_t> g_sav_facmap;
std::map<std::vector<float>, uint32_t> g_sav_uvcmap;
std::map<std::string, uint32_t> g_sav_ftxmap;

void MakeObjChunk(Chunk *c, GameObject *o, bool isclp)
{
	moc_objcount++;
	memset(c, 0, sizeof(Chunk));

	uint32_t posoff;
	std::array<float, 3> cpos = { o->position.x, o->position.y, o->position.z };
	auto piter = g_sav_posmap.find(cpos);
	if (piter == g_sav_posmap.end())
	{
		posoff = posbuf.size() * 12;
		posbuf.push_back(cpos);
		g_sav_posmap[cpos] = posoff;
	}
	else
		posoff = piter->second;

	uint32_t mtxoff;
	std::array<uint32_t, 4> cmtx;
	cmtx[0] = (uint32_t)(o->matrix._31 * 1073741824.0f) & ~1; // multiply by 2^30
	cmtx[1] = o->matrix._32 * 1073741824.0f;
	if (o->matrix._33 < 0) cmtx[0] |= 1;
	cmtx[2] = (uint32_t)(o->matrix._21 * 1073741824.0f) & ~1;
	cmtx[3] = o->matrix._22 * 1073741824.0f;
	if (o->matrix._23 < 0) cmtx[2] |= 1;
	auto miter = g_sav_mtxmap.find(cmtx);
	if (miter == g_sav_mtxmap.end())
	{
		mtxoff = mtxbuf.size();
		mtxbuf.push_back(cmtx);
		g_sav_mtxmap[cmtx] = mtxoff;
	}
	else
		mtxoff = miter->second;

	std::stringbuf dblsav;
	for (auto e = o->dbl.begin(); e != o->dbl.end(); e++)
	{
		uint8_t typ = e->type | e->flags;
		dblsav.sputc(typ);
		switch (e->type)
		{
		case 0:
			break;
		case 1:
			dblsav.sputn((char*)&std::get<double>(e->value), 8); break;
		case 2:
			dblsav.sputn((char*)&std::get<float>(e->value), 4); break;
		case 3:
		case 0xA:
		case 0xB:
		case 0xC:
			dblsav.sputn((char*)&std::get<uint32_t>(e->value), 4); break;
		case 4:
		case 5: {
			auto& str = std::get<std::string>(e->value);
			dblsav.sputn(str.data(), str.size() + 1); break;
		}
		case 6:
		case 0x3f:
			break;
		case 7:
		{
			auto& vec = std::get<std::vector<uint8_t>>(e->value);
			uint32_t siz = (uint32_t)vec.size() + 4;
			dblsav.sputn((char*)&siz, 4);
			dblsav.sputn((char*)vec.data(), vec.size());
			break;
		}
		case 8:
		{
			auto& obj = std::get<GORef>(e->value);
			uint32_t x = objidmap[obj.get()];
			dblsav.sputn((char*)&x, 4); break;
		}
		case 9:
		{
			auto& vec = std::get<std::vector<GORef>>(e->value);
			uint32_t siz = (uint32_t)vec.size() * 4 + 4;
			dblsav.sputn((char*)&siz, 4);
			for (auto& obj : vec) {
				uint32_t x = objidmap[obj.get()];
				dblsav.sputn((char*)&x, 4);
			}
			break;
		}
		}
	}
	uint32_t dbloff;
	std::pair<uint32_t, std::string> cdbl(o->dblflags, dblsav.str());
	auto diter = g_sav_dblmap.find(cdbl);
	if (diter == g_sav_dblmap.end())
	{
		dbloff = sbtell(&dblbuf);
		g_sav_dblmap.insert(std::make_pair(cdbl, dbloff)); //dblmap[cdbl] = dbloff;

		uint32_t siz = sbtell(&dblsav) + 4;
		dblbuf.sputn((char*)&siz, 3);
		dblbuf.sputn((char*)&o->dblflags, 1);
		dblbuf.sputn(cdbl.second.data(), siz - 4);
	}
	else
		dbloff = diter->second;

	uint32_t heaoff = sbtell(&heabuf);

	auto itNammap = g_sav_nammap.find(o->name);
	uint32_t namoff = 0;
	if (itNammap == g_sav_nammap.end()) {
		namoff = sbtell(&nambuf);
		g_sav_nammap.insert({ o->name, namoff });
		nambuf.sputn(o->name.data(), o->name.size() + 1);
	}
	else
		namoff = itNammap->second;

	if(true)
	{
		heabuf.sputn((char*)&dbloff, 4);
		heabuf.sputn((char*)&o->pexcoff, 4);
		heabuf.sputn((char*)&namoff, 4);
		heabuf.sputn((char*)&mtxoff, 4);
		heabuf.sputn((char*)&posoff, 4);
		heabuf.sputn((char*)&o->type, 2);
		heabuf.sputn((char*)&o->flags, 2);

		auto look = [](auto& buf, auto& map, auto& data, auto& outOffset, int multiplier) {
			if (data.empty()) {
				outOffset = 0;
				return;
			}
			if (auto it = map.find(data); it != map.end()) {
				outOffset = it->second;
			}
			else {
				outOffset = buf.size() * multiplier;
				buf.insert(buf.end(), data.begin(), data.end());
				map.emplace(data, outOffset);
			}
		};

		if (o->flags & 0x0020)
		{
			assert(o->mesh);
			uint32_t vertstart, quadstart, tristart, ftxo = 0;
			uint32_t numverts = (uint32_t)o->mesh->vertices.size();
			uint32_t numquads = (uint32_t)o->mesh->quadIndices.size() / 4;
			uint32_t numtris = (uint32_t)o->mesh->triIndices.size() / 3;
			uint32_t uv1start, uv2start, numFaces = numquads + numtris;
			assert(o->mesh->ftxFaces.empty() || (numFaces == o->mesh->ftxFaces.size()));

			look(g_sav_verbuf, g_sav_vermap, o->mesh->vertices, vertstart, 3);
			look(g_sav_facbuf, g_sav_facmap, o->mesh->triIndices, tristart, 1);
			look(g_sav_facbuf, g_sav_facmap, o->mesh->quadIndices, quadstart, 1);
			look(g_sav_uvcbuf, g_sav_uvcmap, o->mesh->primaryUvs, uv1start, 1);
			look(g_sav_uvcbuf, g_sav_uvcmap, o->mesh->secondaryUvs, uv2start, 1);

			if (!o->mesh->ftxFaces.empty()) {
				std::stringbuf ftxdata;
				ftxdata.sputn((char*)&uv1start, 4);
				ftxdata.sputn((char*)&uv2start, 4);
				ftxdata.sputn((char*)&numFaces, 4);
				ftxdata.sputn((char*)o->mesh->ftxFaces.data(), numFaces * 12);
				auto ftxstr = ftxdata.str();
				
				if (auto it = g_sav_ftxmap.find(ftxstr); it == g_sav_ftxmap.end()) {
					uint32_t off = sbtell(&g_sav_ftxbuf) + 1;
					g_sav_ftxmap.insert({ ftxstr, off });
					g_sav_ftxbuf.sputn(ftxstr.data(), ftxstr.size());
					ftxo = off;
				}
				else
					ftxo = it->second;
			}

			heabuf.sputn((char*)&vertstart, 4);
			heabuf.sputn((char*)&quadstart, 4);
			heabuf.sputn((char*)&tristart, 4);
			heabuf.sputn((char*)&ftxo, 4);
			heabuf.sputn((char*)&numverts, 4);
			heabuf.sputn((char*)&numquads, 4);
			heabuf.sputn((char*)&numtris, 4);
			heabuf.sputn((char*)&o->color, 4);
			heabuf.sputn((char*)&o->mesh->flags, 4);
		}
		if (o->flags & 0x0400) {
			assert(o->shape);
			uint32_t vertstart;
			uint32_t numverts = (uint32_t)o->shape->vertices.size();
			look(g_sav_verbuf, g_sav_vermap, o->shape->vertices, vertstart, 3);
			heabuf.sputn((char*)&vertstart, 4);
			heabuf.sputn((char*)&o->shape->quadstart, 4);
			heabuf.sputn((char*)&o->shape->tristart, 4);
			heabuf.sputn((char*)&o->shape->ftxo, 4);
			heabuf.sputn((char*)&numverts, 4);
			heabuf.sputn((char*)&o->shape->numquads, 4);
			heabuf.sputn((char*)&o->shape->numtris, 4);
			heabuf.sputn((char*)&o->color, 4);
			heabuf.sputn((char*)&o->shape->weird, 4);
		}
		if (o->flags & 0x0080)
		{
			assert(o->light);
			for (int i = 0; i < 7; i++)
				heabuf.sputn((char*)&o->light->param[i], 4);
		}
	}
	uint32_t tagstate = o->state | (isclp ? 1 : 0);
	c->tag = heaoff | (tagstate << 24);
	c->subchunks.resize(o->subobj.size());
	int i = 0;
	for(auto e = o->subobj.begin(); e != o->subobj.end(); e++)
	{
		Chunk *s = &c->subchunks[i];
		s->tag = 0;
		MakeObjChunk(s, *e, isclp);
		i++;
	}
}

void ModifySPK()
{
	Chunk nrot, nclp;
	nrot.tag = 'TORP';
	nclp.tag = 'PLCP';

	uint32_t objid = 1;
	std::function<void(GameObject*)> z;
	z = [&z,&objid](GameObject *o) {
		for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
		{
			objidmap[*e] = objid++;
			z(*e);
		}
	};
	z(cliprootobj);
	z(rootobj);

	g_sav_posmap.clear();
	g_sav_mtxmap.clear();
	g_sav_nammap.clear();
	g_sav_dblmap.clear();
	g_sav_vermap.clear();
	g_sav_facmap.clear();
	g_sav_uvcmap.clear();
	g_sav_ftxmap.clear();

	auto f = [](Chunk *c, GameObject *o) {
		c->subchunks.resize(o->subobj.size());
		moc_objcount = 0;
		int i = 0;
		for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
		{
			Chunk *s = &c->subchunks[i++];
			MakeObjChunk(s, *e, o==cliprootobj);
		}
		c->maindata.resize(4);
		*(uint32_t*)c->maindata.data() = moc_objcount;
	};
	f(&nrot, rootobj);
	f(&nclp, cliprootobj);

	*prot = std::move(nrot);
	*pclp = std::move(nclp);

	auto fillMaindata = [](uint32_t tag, Chunk *nthg, const auto& buf) {
		using T = std::remove_reference_t<decltype(buf)>;
		nthg->tag = tag;
		nthg->maindata.resize(buf.size() * sizeof(T::value_type));
		memcpy(nthg->maindata.data(), buf.data(), nthg->maindata.size());
	};

	Chunk nhea, nnam, npos, nmtx, ndbl, nver, nfac, nuvc, nftx;
	fillMaindata('AEHP', &nhea, heabuf.str());
	fillMaindata('MANP', &nnam, nambuf.str());
	fillMaindata('SOPP', &npos, posbuf);
	fillMaindata('XTMP', &nmtx, mtxbuf);
	fillMaindata('LBDP', &ndbl, dblbuf.str());
	fillMaindata('REVP', &nver, g_sav_verbuf);
	fillMaindata('CAFP', &nfac, g_sav_facbuf);
	fillMaindata('CVUP', &nuvc, g_sav_uvcbuf);
	fillMaindata('XTFP', &nftx, g_sav_ftxbuf.str());

	// Chunk comparison
	auto chkcmp = [](Chunk* chka, Chunk* chkb, const char* name) {
		printf("----- Comparison of old and new %s -----\n", name);
		if (chka->tag != chkb->tag)
			printf("Different tag\n");
		if (chka->multidata.size() != chkb->multidata.size())
			printf("Different num_datas: %zu -> %zu\n", chka->multidata.size(), chkb->multidata.size());
		if (chka->maindata.size() != chkb->maindata.size())
			printf("Different maindata_size: %zu -> %zu\n", chka->maindata.size(), chkb->maindata.size());
		else if(chka->maindata.size()) {
			uint32_t mdcmp = 0;
			for (size_t i = 0; i < (size_t)chka->maindata.size(); i++)
				if (((char*)chka->maindata.data())[i] != ((char*)chkb->maindata.data())[i])
					mdcmp += 1;
			if (mdcmp != 0)
				printf("Different maindata content: %u bytes are different\n", mdcmp);
			auto sum_a = checksum(chka->maindata.data(), chka->maindata.size());
			auto sum_b = checksum(chkb->maindata.data(), chkb->maindata.size());
			if (sum_a != sum_b)
				printf("Different checksum\n");
			else
				printf("Same checksum\n");
		}
	};
	chkcmp(phea, &nhea, "PHEA");
	chkcmp(pnam, &nnam, "PNAM");
	chkcmp(ppos, &npos, "PPOS");
	chkcmp(pmtx, &nmtx, "PMTX");
	chkcmp(pdbl, &ndbl, "PDBL");
	chkcmp(pver, &nver, "PVER");
	chkcmp(pfac, &nfac, "PFAC");
	chkcmp(puvc, &nuvc, "PUVC");
	chkcmp(pftx, &nftx, "PFTX");

	*phea = std::move(nhea);
	*pnam = std::move(nnam);
	*ppos = std::move(npos);
	*pmtx = std::move(nmtx);
	*pdbl = std::move(ndbl);
	*pver = std::move(nver);
	*pfac = std::move(nfac);
	*puvc = std::move(nuvc);
	*pftx = std::move(nftx);

	((uint32_t*)spkchk->maindata.data())[1] = 0x40000;

	objidmap.clear();
}

void SaveSceneSPK(const char *fn)
{
	mz_zip_archive inzip, outzip;
	mz_zip_zero_struct(&inzip);
	mz_zip_zero_struct(&outzip);

	mz_bool mzr;
	mzr = mz_zip_reader_init_mem(&inzip, zipmem, zipsize, 0);
	if (!mzr) { warn("Couldn't reopen the original scene ZIP file."); return; }
	mzr = mz_zip_writer_init_file(&outzip, fn, 0);
	if (!mzr) { warn("Couldn't create the new scene ZIP file for saving."); return; }

	int nfiles = mz_zip_reader_get_num_files(&inzip);
	int spkindex = mz_zip_reader_locate_file(&inzip, "Pack.SPK", 0, 0);
	assert(spkindex != -1);
	for (int i = 0; i < nfiles; i++)
		if (i != spkindex)
			mz_zip_writer_add_from_zip_reader(&outzip, &inzip, i);

	ModifySPK();
	void *spkmem; size_t spksize;
	spkchk->saveToMem(&spkmem, &spksize);

	mz_zip_writer_add_mem(&outzip, "Pack.SPK", spkmem, spksize, MZ_DEFAULT_COMPRESSION);
	mz_zip_writer_finalize_archive(&outzip);
	mz_zip_writer_end(&outzip);
	mz_zip_reader_end(&inzip);

	heabuf = std::stringbuf();
	nambuf = std::stringbuf();
	posbuf.clear(); mtxbuf.clear(); dblbuf = std::stringbuf();
	free(spkmem);
}

void RemoveObject(GameObject *o)
{
	if (o->parent)
	{
		auto &st = o->parent->subobj;
		auto it = std::find(o->parent->subobj.begin(), o->parent->subobj.end(), o);
		assert(it != o->parent->subobj.end());
		o->parent->subobj.erase(it);
	}
	delete o;
}

GameObject* DuplicateObject(GameObject *o, GameObject *parent)
{
	if (!o->parent) return 0;
	GameObject *d = new GameObject(*o);
	
	d->refcount = 0;
	d->subobj.clear();
	d->parent = parent;
	parent->subobj.push_back(d);
	for (int i = 0; i < o->subobj.size(); i++)
		DuplicateObject(o->subobj[i], d);

	return d;
}

void GiveObject(GameObject *o, GameObject *t)
{
	if (o->parent)
	{
		auto &st = o->parent->subobj;
		auto it = std::find(st.begin(), st.end(), o);
		if (it != st.end())
			st.erase(it);
	}
	t->subobj.push_back(o);
}

const char * DBLEntry::getTypeName(int type)
{
	type &= 0x3F;
	static const char* names[] = { "0", "double", "float", "int", "char*", "5", "EndCpnt", "7", "ZGEOMREF", "ZGEOMREFTAB", "MSG", "SNDREF", "INTC" };
	if (type == 0x3F)
		return "EndDBL";
	if (type < std::size(names))
		return names[type];
	return "?";
}

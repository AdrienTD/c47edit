// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "global.h"
#include "miniz.h"
#include <functional>
#include <sstream>
#include <array>
#include <map>
#include <unordered_map>

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
void *zipmem = 0; uint zipsize = 0;

const char *GetObjTypeString(uint ot)
{
	const char *otname = "?";
	if (ot < 0x70) otname = objtypenames[ot];
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
	LoadChunk(spkchk, spkmem);
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
	std::map<uint, GameObject*> idobjmap;
	std::map<Chunk*, GameObject*> chkobjmap;
	std::function<void(Chunk*,GameObject*)> z;
	uint objid = 1;
	z = [&z, &objid, &chkobjmap, &idobjmap](Chunk *c, GameObject *parentobj) {
		uint pheaoff = c->tag & 0xFFFFFF;
		uint *p = (uint*)((char*)phea->maindata + pheaoff);
		uint ot = *(unsigned short*)(&p[5]);
		char *objname = (char*)pnam->maindata + p[2];

		GameObject *o = new GameObject(objname, ot);
		chkobjmap[c] = o;
		parentobj->subobj.push_back(o);
		o->parent = parentobj;
		idobjmap[objid++] = o;
		if (c->num_subchunks > 0)
			for (uint i = 0; i < c->num_subchunks; i++)
				z(&c->subchunks[i], o);
	};

	auto y = [z](Chunk *c, GameObject *o) {
		for (uint i = 0; i < c->num_subchunks; i++)
			z(&c->subchunks[i], o);
	};

	y(pclp, cliprootobj);
	y(prot, rootobj);

	// Then read/load the object properties.
	std::function<void(Chunk*, GameObject*)> g;
	g = [&g,&objid,&chkobjmap,&idobjmap](Chunk *c, GameObject *parentobj) {
		uint pheaoff = c->tag & 0xFFFFFF;
		uint *p = (uint*)((char*)phea->maindata + pheaoff);
		uint ot = *(unsigned short*)(&p[5]);
		const char *otname = GetObjTypeString(ot);
		char *objname = (char*)pnam->maindata + p[2];

		GameObject *o = chkobjmap[c];
		o->state = (c->tag >> 24) & 255;
		o->pdbloff = p[0];
		o->pexcoff = p[1];
		o->flags = *((unsigned short*)(&p[5]) + 1);
		o->color = p[13];
		o->root = o->parent->root;

		o->position = *(Vector3*)((char*)ppos->maindata + p[4]);
		CreateIdentityMatrix(&o->matrix);
		float mc[4];
		int32_t *mtxoff  = (int32_t*)pmtx->maindata + p[3] * 4;
		for (int i = 0; i < 4; i++)
			mc[i] = (float)((double)mtxoff[i] / 1073741824.0); // divide by 2^30
		Vector3 rv[3];
		rv[2] = Vector3(mc[0], mc[1], 1 - mc[0]*mc[0] - mc[1]*mc[1]);
		rv[1] = Vector3(mc[2], mc[3], 1 - mc[2]*mc[2] - mc[3]*mc[3]);
		if (mtxoff[0] & 1) rv[2].z = -rv[2].z;
		if (mtxoff[2] & 1) rv[1].z = -rv[1].z;
		Vec3Cross(&rv[0], &rv[1], &rv[2]);
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				o->matrix.m[i][j] = rv[i].c[j];

		if (o->flags & 0x0420)
		{
			Mesh *m = o->mesh = new Mesh;
			m->vertstart = p[6]; m->quadstart = p[7]; m->tristart = p[8];
			m->ftxo = p[9];
			m->numverts = p[10]; m->numquads = p[11]; m->numtris = p[12];
			m->weird = p[14];
		}

		if (o->flags & 0x0080)
		{
			Light *l = o->light = new Light;
			for (int i = 0; i < 7; i++)
				l->param[i] = p[6 + i];
		}

		char *dpbeg = (char*)pdbl->maindata + p[0];
		uint32_t ds = *(uint32_t*)dpbeg & 0xFFFFFF;
		o->dblflags = (*(uint32_t*)dpbeg >> 24) & 255;
		char *dp = dpbeg + 4;
		while (dp - dpbeg < ds)
		{
			DBLEntry e;
			e.type = *dp & 0x3F;
			e.flags = *dp & 0xC0;
			dp++;
			switch (e.type)
			{
			case 1:
				e.dbl = *(double*)dp; dp += 8; break;
			case 2:
				e.flt = *(float*)dp; dp += 4; break;
			case 3:
			case 0xA:
			case 0xB:
			case 0xC:
				e.u32 = *(uint32_t*)dp; dp += 4; break;
			case 4:
			case 5:
				e.str = strdup(dp); while (*(dp++)); break;
			case 6:
				break;
			case 7:
				e.datsize = *(uint32_t*)dp - 4;
				e.datpnt = malloc(e.datsize);
				memcpy(e.datpnt, dp + 4, e.datsize);
				dp += *(uint32_t*)dp;
				break;
			case 8:
				e.obj = idobjmap[*(uint32_t*)dp]; dp += 4; break;
			case 9:
				e.nobjs = (*(uint32_t*)dp - 4) / 4;
				e.objlist = new goref[e.nobjs];
				for (int i = 0; i < e.nobjs; i++)
					e.objlist[i] = idobjmap[*(uint32_t*)(dp+4+4*i)];
				dp += *(uint32_t*)dp;
				break;
			case 0x3F:
				break;
			default:
				ferr("Unknown DBL entry type!");
			}
			o->dbl.push_back(e);
		}

		if (c->num_subchunks > 0)
		{
			for (uint i = 0; i < c->num_subchunks; i++)
				g(&c->subchunks[i], o);
		}
	};

	auto f = [g](Chunk *c, GameObject *o) {
		for (uint i = 0; i < c->num_subchunks; i++)
			g(&c->subchunks[i], o);
	};

	f(pclp, cliprootobj);
	f(prot, rootobj);
}

struct DBLHash
{
	size_t operator() (std::pair<uint32_t, std::string> e) const
	{
		const std::hash<std::string> h;
		return e.first + h(e.second);
	}
};

std::stringbuf heabuf, nambuf, dblbuf;
std::vector<std::array<float,3> > posbuf;
std::vector<std::array<uint, 4> > mtxbuf;

uint sbtell(std::stringbuf *sb);

uint moc_objcount;
std::map<GameObject*, uint32_t> objidmap;

std::map<std::array<float, 3>, uint32_t> g_sav_posmap;
std::map<std::array<uint, 4>, uint32_t> g_sav_mtxmap;
std::map<std::string, uint32_t> g_sav_nammap;
std::unordered_map<std::pair<uint32_t, std::string>, uint32_t, DBLHash> g_sav_dblmap;

void MakeObjChunk(Chunk *c, GameObject *o, bool isclp)
{
	moc_objcount++;
	memset(c, 0, sizeof(Chunk));

	uint posoff;
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

	uint mtxoff;
	std::array<uint, 4> cmtx;
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
		case 1:
			dblsav.sputn((char*)&e->dbl, 8); break;
		case 2:
			dblsav.sputn((char*)&e->flt, 4); break;
		case 3:
		case 0xA:
		case 0xB:
		case 0xC:
			dblsav.sputn((char*)&e->u32, 4); break;
		case 4:
		case 5:
			dblsav.sputn(e->str, strlen(e->str) + 1); break;
		case 6:
		case 0x3f:
			break;
		case 7:
		{
			uint32_t siz = e->datsize + 4;
			dblsav.sputn((char*)&siz, 4);
			dblsav.sputn((char*)e->datpnt, e->datsize);
			break;
		}
		case 8:
		{
			uint32_t x = objidmap[e->obj.get()];
			dblsav.sputn((char*)&x, 4); break;
		}
		case 9:
		{
			uint32_t siz = e->nobjs * 4 + 4;
			dblsav.sputn((char*)&siz, 4);
			for (int i = 0; i < e->nobjs; i++)
				dblsav.sputn((char*)&(objidmap[e->objlist[i].get()]), 4);
			break;
		}
		}
	}
	uint dbloff;
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

	uint heaoff = sbtell(&heabuf);

	auto itNammap = g_sav_nammap.find(o->name);
	uint namoff = 0;
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
		if (o->flags & 0x0420)
		{
			assert(o->mesh);
			heabuf.sputn((char*)&o->mesh->vertstart, 4);
			heabuf.sputn((char*)&o->mesh->quadstart, 4);
			heabuf.sputn((char*)&o->mesh->tristart, 4);
			heabuf.sputn((char*)&o->mesh->ftxo, 4);
			heabuf.sputn((char*)&o->mesh->numverts, 4);
			heabuf.sputn((char*)&o->mesh->numquads, 4);
			heabuf.sputn((char*)&o->mesh->numtris, 4);
			heabuf.sputn((char*)&o->color, 4);
			heabuf.sputn((char*)&o->mesh->weird, 4);
		}
		if (o->flags & 0x0080)
		{
			assert(o->light);
			for (int i = 0; i < 7; i++)
				heabuf.sputn((char*)&o->light->param[i], 4);
		}
	}
	uint tagstate = o->state | (isclp ? 1 : 0);
	c->tag = heaoff | (tagstate << 24);
	c->num_subchunks = o->subobj.size();
	c->subchunks = new Chunk[c->num_subchunks];
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
	Chunk *nrot, *nclp;
	nrot = new Chunk;
	nclp = new Chunk;
	memset(nrot, 0, sizeof(Chunk));
	memset(nclp, 0, sizeof(Chunk));
	nrot->tag = 'TORP';
	nclp->tag = 'PLCP';

	uint objid = 1;
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

	auto f = [](Chunk *c, GameObject *o) {
		c->num_subchunks = o->subobj.size();
		c->subchunks = new Chunk[c->num_subchunks];
		moc_objcount = 0;
		int i = 0;
		for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
		{
			Chunk *s = &c->subchunks[i++];
			MakeObjChunk(s, *e, o==cliprootobj);
		}
		c->maindata = malloc(4);
		c->maindata_size = 4;
		*(uint32_t*)c->maindata = moc_objcount;
	};
	f(nrot, rootobj);
	f(nclp, cliprootobj);

	*prot = *nrot;
	*pclp = *nclp;

	auto g = [](Chunk *nthg, std::stringbuf &buf) {
		std::string st = buf.str();
		nthg->maindata = malloc(st.length());
		nthg->maindata_size = st.length();
		memcpy(nthg->maindata, st.data(), nthg->maindata_size);
	};

	Chunk *nhea, *nnam, *npos, *nmtx, *ndbl;
	nhea = new Chunk; memset(nhea, 0, sizeof(Chunk));
	nnam = new Chunk; memset(nnam, 0, sizeof(Chunk));
	nhea->tag = 'AEHP';
	nnam->tag = 'MANP';
	g(nhea, heabuf);
	g(nnam, nambuf);

	npos = new Chunk; memset(npos, 0, sizeof(Chunk));
	npos->tag = 'SOPP';
	npos->maindata = posbuf.data();
	npos->maindata_size = posbuf.size() * 12;

	nmtx = new Chunk; memset(nmtx, 0, sizeof(Chunk));
	nmtx->tag = 'XTMP';
	nmtx->maindata = mtxbuf.data();
	nmtx->maindata_size = mtxbuf.size() * 16;

	ndbl = new Chunk; memset(ndbl, 0, sizeof(Chunk));
	ndbl->tag = 'LBDP';
	std::string dblstr = dblbuf.str();
	ndbl->maindata = malloc(dblstr.size());
	memcpy(ndbl->maindata, dblstr.data(), dblstr.size());
	ndbl->maindata_size = dblstr.size();

	// Chunk comparison
	auto chkcmp = [](Chunk* chka, Chunk* chkb, const char* name) {
		printf("----- Comparison of old and new %s -----\n", name);
		if (chka->tag != chkb->tag)
			printf("Different tag\n");
		if (chka->num_datas != chkb->num_datas)
			printf("Different num_datas: %u -> %u\n", chka->num_datas, chkb->num_datas);
		if (chka->maindata_size != chkb->maindata_size)
			printf("Different maindata_size: %u -> %u\n", chka->maindata_size, chkb->maindata_size);
		else if(chka->maindata_size) {
			uint32_t mdcmp = 0;
			for (size_t i = 0; i < (size_t)chka->maindata_size; i++)
				if (((char*)chka->maindata)[i] != ((char*)chkb->maindata)[i])
					mdcmp += 1;
			if (mdcmp != 0)
				printf("Different maindata content: %u bytes are different\n", mdcmp);
		}
	};
	chkcmp(phea, nhea, "PHEA");
	chkcmp(pnam, nnam, "PNAM");
	chkcmp(ppos, npos, "PPOS");
	chkcmp(pmtx, nmtx, "PMTX");
	chkcmp(pdbl, ndbl, "PDBL");

	*phea = *nhea;
	*pnam = *nnam;
	*ppos = *npos;
	*pmtx = *nmtx;
	*pdbl = *ndbl;

	((uint32_t*)spkchk->maindata)[1] = 0x40000;

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
	SaveChunkToMem(spkchk, &spkmem, &spksize);

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
		if (it != o->parent->subobj.end())
			o->parent->subobj.erase(it);
	}
	for (auto e = o->dbl.begin(); e != o->dbl.end(); e++)
	{
		switch (e->type)
		{
		case 4:
		case 5:
			free(e->str); break;
		case 7:
			free(e->datpnt); break;
		case 8:
			e->obj.deref(); break;
		case 9:
			for (int i = 0; i < e->nobjs; i++)
				e->objlist[i].deref();
			delete[] e->objlist;
			break;
		}
	}
	delete o;
}

GameObject* DuplicateObject(GameObject *o, GameObject *parent)
{
	if (!o->parent) return 0;
	GameObject *d = new GameObject;
	*d = *o;
	d->name = o->name;
	
	d->dbl.clear();
	for (auto e = o->dbl.begin(); e != o->dbl.end(); e++)
	{
		DBLEntry n;
		n.type = e->type; n.flags = e->flags;
		switch (n.type)
		{
		case 1:
			n.dbl = e->dbl; break;
		case 2:
			n.flt = e->flt; break;
		case 3: case 0xA: case 0xB: case 0xC:
			n.u32 = e->u32; break;
		case 4: case 5:
			n.str = strdup(e->str); break;
		case 7:
			n.datsize = e->datsize;
			n.datpnt = malloc(n.datsize);
			memcpy(n.datpnt, e->datpnt, n.datsize);
			break;
		case 8:
			n.obj = e->obj.get(); break;
		case 9:
			n.nobjs = e->nobjs;
			n.objlist = new goref[n.nobjs];
			for (int i = 0; i < n.nobjs; i++)
				n.objlist[i] = e->objlist[i].get();
			break;
		}
		d->dbl.push_back(*e);
	}

	d->subobj.clear();
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

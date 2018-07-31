#include "global.h"
#include "miniz.h"
#include <functional>
#include <sstream>
#include <array>
#include <map>

char *objtypenames[] = {
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
Chunk *prot, *pclp, *phea, *pnam, *ppos, *pmtx, *pver, *pfac;
GameObject *rootobj, *cliprootobj, *superroot;
char *lastspkfn = 0;

char *GetObjTypeString(uint ot)
{
	char *otname = "?";
	if (ot < 0x70) otname = objtypenames[ot];
	return otname;
}

void LoadSceneSPK(char *fn)
{
	mz_zip_archive zip; void *spkmem; size_t spksize;
	spkchk = new Chunk;
	mz_zip_zero_struct(&zip);
	mz_bool mzreadok = mz_zip_reader_init_file(&zip, fn, 0);
	if (!mzreadok) ferr("Failed to initialize ZIP reading.");
	spkmem = mz_zip_reader_extract_file_to_heap(&zip, "Pack.SPK", &spksize, 0);
	if (!spkmem) ferr("Failed to extract Pack.SPK from ZIP archive.");
	mz_zip_reader_end(&zip);
	LoadChunk(spkchk, spkmem);
	free(spkmem);
	lastspkfn = strdup(fn);

	prot = spkchk->findSubchunk('TORP');
	pclp = spkchk->findSubchunk('PLCP');
	phea = spkchk->findSubchunk('AEHP');
	pnam = spkchk->findSubchunk('MANP');
	ppos = spkchk->findSubchunk('SOPP');
	pmtx = spkchk->findSubchunk('XTMP');
	pver = spkchk->findSubchunk('REVP');
	pfac = spkchk->findSubchunk('CAFP');
	if (!(prot && pclp && phea && pnam && ppos && pmtx && pver && pfac))
		ferr("One or more important chunks were not found in Pack.SPK .");

	rootobj = new GameObject("Root", 0x21 /*ZROOM*/);
	cliprootobj = new GameObject("ClipRoot", 0x21 /*ZROOM*/);
	superroot = new GameObject("SuperRoot", 0x21);
	superroot->subobj.push_back(rootobj);
	superroot->subobj.push_back(cliprootobj);
	rootobj->parent = cliprootobj->parent = superroot;
	rootobj->root = rootobj;
	cliprootobj->root = cliprootobj;

	std::function<void(Chunk*, GameObject*)> g;
	g = [&g](Chunk *c, GameObject *parentobj) {
		uint pheaoff = c->tag & 0xFFFFFF;
		uint *p = (uint*)((char*)phea->maindata + pheaoff);
		uint ot = *(unsigned short*)(&p[5]);
		char *otname = GetObjTypeString(ot);
		char *objname = (char*)pnam->maindata + p[2];

		GameObject *o = new GameObject(objname, ot);
		parentobj->subobj.push_back(o);
		o->parent = parentobj;
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
			mc[i] = (float)mtxoff[i] / 1073741824.0f; // divide by 2^30
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

		if (c->num_subchunks > 0)
		{
			for (uint i = 0; i < c->num_subchunks; i++)
				g(&c->subchunks[i], o);
		}
	};

	auto f = [g](Chunk *c, GameObject *o) {
		char s[5]; *(uint*)s = c->tag; s[4] = 0;
		for (uint i = 0; i < c->num_subchunks; i++)
			g(&c->subchunks[i], o);
	};

	f(prot, rootobj);
	f(pclp, cliprootobj);
}

std::stringbuf heabuf, nambuf;
std::vector<std::array<float,3> > posbuf;
std::vector<std::array<uint, 4> > mtxbuf;
std::map<std::array<float, 3>, uint32_t> posmap;
std::map<std::array<uint, 4>, uint32_t> mtxmap;

uint sbtell(std::stringbuf *sb);

uint moc_objcount;

void MakeObjChunk(Chunk *c, GameObject *o, bool isclp)
{
	moc_objcount++;
	memset(c, 0, sizeof(Chunk));

	uint posoff;
	std::array<float, 3> cpos = { o->position.x, o->position.y, o->position.z };
	auto piter = posmap.find(cpos);
	if (piter == posmap.end())
	{
		posoff = posbuf.size() * 12;
		posbuf.push_back(cpos);
		posmap[cpos] = posoff;
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
	auto miter = mtxmap.find(cmtx);
	if (miter == mtxmap.end())
	{
		mtxoff = mtxbuf.size();
		mtxbuf.push_back(cmtx);
		mtxmap[cmtx] = mtxoff;
	}
	else
		mtxoff = miter->second;

	uint heaoff = sbtell(&heabuf);
	uint namoff = sbtell(&nambuf);
		
	nambuf.sputn(o->name, strlen(o->name) + 1);
	if(true)
	{
		heabuf.sputn((char*)&o->pdbloff, 4);
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

	Chunk *nhea, *nnam, *npos, *nmtx;
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

	*phea = *nhea;
	*pnam = *nnam;
	*ppos = *npos;
	*pmtx = *nmtx;

	((uint32_t*)spkchk->maindata)[1] = 0x40000;

	heabuf = std::stringbuf();
	nambuf = std::stringbuf();
	posbuf.clear(); mtxbuf.clear();
	posmap.clear(); mtxmap.clear();
}

void SaveSceneSPK(char *fn)
{
	ModifySPK();

	void *spkmem; size_t spksize;
	SaveChunkToMem(spkchk, &spkmem, &spksize);

	mz_zip_archive inzip, outzip;
	mz_zip_zero_struct(&inzip);
	mz_zip_zero_struct(&outzip);
	mz_bool mzr;
	mzr = mz_zip_reader_init_file(&inzip, lastspkfn, 0);
	if (!mzr) { warn("Couldn't reopen the original scene ZIP file."); free(spkmem); return; }
	mzr = mz_zip_writer_init_file(&outzip, fn, 0);
	if (!mzr) { warn("Couldn't create the new scene ZIP file for saving."); free(spkmem); return; }
	int nfiles = mz_zip_reader_get_num_files(&inzip);
	int spkindex = mz_zip_reader_locate_file(&inzip, "Pack.SPK", 0, 0);
	assert(spkindex != -1);
	for (int i = 0; i < nfiles; i++)
		if (i != spkindex)
			mz_zip_writer_add_from_zip_reader(&outzip, &inzip, i);
	mz_zip_writer_add_mem(&outzip, "Pack.SPK", spkmem, spksize, MZ_DEFAULT_COMPRESSION);
	mz_zip_writer_finalize_archive(&outzip);
	mz_zip_writer_end(&outzip);
	mz_zip_reader_end(&inzip);

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
	delete o;
}

GameObject* DuplicateObject(GameObject *o, GameObject *parent)
{
	if (!o->parent) return 0;
	GameObject *d = new GameObject;
	*d = *o;
	d->name = strdup(o->name);
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
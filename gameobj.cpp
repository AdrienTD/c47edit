// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include <functional>
#include <array>
#include <map>
#include <unordered_map>

#include "global.h"
#include "gameobj.h"
#include "chunk.h"
#include "vecmat.h"
#include "ByteWriter.h"
#include "classInfo.h"

#include <miniz/miniz.h>

std::unordered_map<GameObject*, size_t> g_objRefCounts;
Scene g_scene;

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

const char *GetObjTypeString(uint32_t ot)
{
	const char *otname = "?";
	if (ot < std::size(objtypenames)) otname = objtypenames[ot];
	return otname;
}

uint32_t ComputeBytesum(void* data, size_t length) {
	uint32_t sum = 0;
	uint8_t* bytes = (uint8_t*)data;
	for (size_t i = 0; i < length; ++i)
		sum += bytes[i];
	return sum;
}

static void ReadAssetPacks(Scene* scene, mz_zip_archive* zip)
{
	static const auto readFile = [](const char* filename) {
		void* repmem; size_t repsize;
		FILE* repfile = fopen(filename, "rb");
		if (!repfile) ferr("Could not open Repeat.* file.\nBe sure you copied all the 4 files named \"Repeat\" (with .ANM, .DXT, .PAL, .WAV extensions) from the Hitman C47 game's folder into the editor's folder (where c47edit.exe is).");
		fseek(repfile, 0, SEEK_END);
		repsize = ftell(repfile);
		fseek(repfile, 0, SEEK_SET);
		repmem = malloc(repsize);
		fread(repmem, repsize, 1, repfile);
		fclose(repfile);
		return std::make_pair(repmem, repsize);
	};

	const auto readPack = [&zip](const char* ext, Chunk& pack, bool* outFound = nullptr) {
		std::string fnPackRepeat = std::string("PackRepeat.") + ext;
		std::string fnRepeat = std::string("Repeat.") + ext;
		std::string fnPack = std::string("Pack.") + ext;
		void* packmem; size_t packsize;
		packmem = mz_zip_reader_extract_file_to_heap(zip, fnPackRepeat.c_str(), &packsize, 0);
		if (packmem)
		{
			auto [repmem, repsize] = readFile(fnRepeat.c_str());
			pack = Chunk::reconstructPackFromRepeat(packmem, packsize, repmem);
			free(repmem);
		}
		else
		{
			packmem = mz_zip_reader_extract_file_to_heap(zip, fnPack.c_str(), &packsize, 0);
			if (!packmem && !outFound) ferr("Failed to find Pack.* or PackRepeat.* in ZIP archive.");
			if (packmem)
				pack.load(packmem);
		}
		if (outFound)
			*outFound = packmem;
		if (packmem)
			free(packmem);
	};

	readPack("PAL", scene->palPack);
	readPack("DXT", scene->dxtPack);
	readPack("LGT", scene->lgtPack);
	readPack("WAV", scene->wavPack);
	readPack("ANM", scene->anmPack, &scene->hasAnmPack);

	if (scene->palPack.tag != 'PAL') ferr("Not a PAL chunk in Repeat.PAL");
	if (scene->dxtPack.tag != 'DXT') ferr("Not a DXT chunk in Repeat.DXT");
	if (scene->lgtPack.tag != 'LGT') ferr("Not a LGT chunk in Repeat.LGT");
	assert(scene->palPack.subchunks.size() == scene->dxtPack.subchunks.size());
}

void Scene::LoadSceneSPK(const char *fn)
{
	Close();

	FILE *zipfile = fopen(fn, "rb");
	if (!zipfile) ferr("Could not open the ZIP file.");
	fseek(zipfile, 0, SEEK_END);
	size_t zipsize = ftell(zipfile);
	fseek(zipfile, 0, SEEK_SET);
	zipmem.resize(zipsize);
	fread(zipmem.data(), zipsize, 1, zipfile);
	fclose(zipfile);

	mz_zip_archive zip; void *spkmem; size_t spksize;
	mz_zip_zero_struct(&zip);
	mz_bool mzreadok = mz_zip_reader_init_mem(&zip, zipmem.data(), zipsize, 0);
	if (!mzreadok) ferr("Failed to initialize ZIP reading.");
	spkmem = mz_zip_reader_extract_file_to_heap(&zip, "Pack.SPK", &spksize, 0);
	if (!spkmem) ferr("Failed to extract Pack.SPK from ZIP archive.");
	ReadAssetPacks(this, &zip);
	mz_zip_reader_end(&zip);
	spkchk.load(spkmem);
	free(spkmem);
	lastspkfn = fn;

	Chunk* prot = spkchk.findSubchunk('TORP');
	Chunk* pclp = spkchk.findSubchunk('PLCP');
	Chunk* phea = spkchk.findSubchunk('AEHP');
	Chunk* pnam = spkchk.findSubchunk('MANP');
	Chunk* ppos = spkchk.findSubchunk('SOPP');
	Chunk* pmtx = spkchk.findSubchunk('XTMP');
	Chunk* pver = spkchk.findSubchunk('REVP');
	Chunk* pfac = spkchk.findSubchunk('CAFP');
	Chunk* pftx = spkchk.findSubchunk('XTFP');
	Chunk* puvc = spkchk.findSubchunk('CVUP');
	Chunk* pdbl = spkchk.findSubchunk('LBDP');
	Chunk* pdat = spkchk.findSubchunk('TADP');
	Chunk* pexc = spkchk.findSubchunk('CXEP');
	if (!(prot && pclp && phea && pnam && ppos && pmtx && pver && pfac && pftx && puvc && pdbl && pdat && pexc))
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
	z = [this, &z, &objid, &chkobjmap, &idobjmap, &phea, &pnam](Chunk *c, GameObject *parentobj) {
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

	using MeshKey = std::array<uint32_t, 8>;
	auto toMeshKey = [](uint32_t* p) {
		return MeshKey{ p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[14] };
	};
	struct MeshKeyHash {
		size_t operator()(const MeshKey& mi) const noexcept {
			return mi[6];
		}
	};
	std::unordered_map<MeshKey, std::shared_ptr<Mesh>, MeshKeyHash> meshMap;
	std::unordered_map<MeshKey, std::shared_ptr<ObjLine>, MeshKeyHash> lineMap;

	// Then read/load the object properties.
	std::function<void(Chunk*, GameObject*)> g;
	g = [&](Chunk *c, GameObject *parentobj) {
		uint32_t pheaoff = c->tag & 0xFFFFFF;
		uint32_t *p = (uint32_t*)((char*)phea->maindata.data() + pheaoff);
		uint32_t ot = *(unsigned short*)(&p[5]);
		const char *otname = GetObjTypeString(ot);
		char *objname = (char*)pnam->maindata.data() + p[2];

		GameObject *o = chkobjmap[c];
		uint8_t state = (c->tag >> 24) & 255;
		assert(state >= 0 && state < 4);
		o->isIncludedScene = state & 2;
		o->flags = *((unsigned short*)(&p[5]) + 1);
		o->root = o->parent->root;

		Vector3 position = *(Vector3*)((char*)ppos->maindata.data() + p[4]);
		o->matrix = Matrix::getTranslationMatrix(position);
		float mc[4];
		int32_t *mtxoff  = (int32_t*)pmtx->maindata.data() + p[3] * 4;
		for (int i = 0; i < 4; i++)
			mc[i] = (float)((double)mtxoff[i] / 1073741824.0); // divide by 2^30
		Vector3 rv[3];
		rv[2] = Vector3(mc[0], mc[1], std::sqrt(std::max(0.0f, 1.0f - mc[0]*mc[0] - mc[1]*mc[1])));
		rv[1] = Vector3(mc[2], mc[3], std::sqrt(std::max(0.0f, 1.0f - mc[2]*mc[2] - mc[3]*mc[3])));
		if (mtxoff[0] & 1) rv[2].z = -rv[2].z;
		if (mtxoff[2] & 1) rv[1].z = -rv[1].z;
		rv[0] = rv[1].cross(rv[2]);
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				o->matrix.m[i][j] = rv[i].coord[j];

		if (o->flags & 0x0020)
		{
			o->color = p[13];
			auto mi = toMeshKey(p);
			auto [meshIt, isFirstTime] = meshMap.try_emplace(mi);
			if (isFirstTime) {
				meshIt->second = std::make_shared<Mesh>();
				Mesh* m = meshIt->second.get();
				m->weird = p[14];

				float* verts = (float*)pver->maindata.data() + p[6];
				uint16_t* quadInds = (uint16_t*)pfac->maindata.data() + p[7];
				uint16_t* triInds = (uint16_t*)pfac->maindata.data() + p[8];
				m->vertices.resize(3 * p[10]);
				m->quadindices.resize(4 * p[11]);
				m->triindices.resize(3 * p[12]);
				memcpy(m->vertices.data(), verts, 4 * m->vertices.size());
				memcpy(m->quadindices.data(), quadInds, 2 * m->quadindices.size());
				memcpy(m->triindices.data(), triInds, 2 * m->triindices.size());

				uint32_t ftxo = 0;
				if (p[9] & 0x80000000) {
					uint32_t* dat1 = (uint32_t*)(pdat->maindata.data() + (p[9] & 0x7FFFFFFF));
					ftxo = dat1[0];
					m->extension = std::make_unique<Mesh::Extension>();
					m->extension->extUnk2 = dat1[1];
					uint8_t* dat2 = pdat->maindata.data() + dat1[2];
					uint8_t* ptr2 = dat2;
					uint32_t numDings = *(uint32_t*)ptr2; ptr2 += 4;
					m->extension->frames.resize(numDings);
					memcpy(m->extension->frames.data(), ptr2, 8 * numDings);
					ptr2 += numDings * 8;
					m->extension->name = (const char*)ptr2;
				}
				else {
					ftxo = p[9];
				}
				if (ftxo != 0) {
					uint8_t* ftx = pftx->maindata.data() + ftxo - 1;
					uint32_t uv1off = *(uint32_t*)ftx;
					uint32_t uv2off = *(uint32_t*)(ftx + 4);
					uint32_t numFaces = *(uint32_t*)(ftx + 8);
					assert(numFaces == m->getNumTris() + m->getNumQuads());
					float* uv1 = (float*)puvc->maindata.data() + uv1off;
					float* uv2 = (float*)puvc->maindata.data() + uv2off;
					m->ftxFaces.resize(numFaces);
					memcpy(m->ftxFaces.data(), ftx + 12, numFaces * 12);
					uint32_t numTexturedFaces = 0, numLitFaces = 0;
					for (auto& face : m->ftxFaces) {
						if (face[0] & 0x20)
							numTexturedFaces += 1;
						if (face[0] & 0x80)
							numLitFaces += 1;
					}
					m->textureCoords.resize(numTexturedFaces * 8);
					m->lightCoords.resize(numLitFaces * 8);
					memcpy(m->textureCoords.data(), uv1, numTexturedFaces * 8 * 4);
					memcpy(m->lightCoords.data(), uv2, numLitFaces * 8 * 4);
				}
			}
			o->mesh = meshIt->second;
		}

		if (o->flags & 0x0400)
		{
			o->color = p[13];
			auto mi = toMeshKey(p);
			auto [lineIt, isFirstTime] = lineMap.try_emplace(mi);
			if (isFirstTime) {
				lineIt->second = std::make_shared<ObjLine>();
				ObjLine* m = lineIt->second.get();
				assert(p[7] == 0 && p[11] == 0);

				m->vertices.resize(3 * p[10]);
				m->terms.resize(p[12]);
				float* verts = (float*)pver->maindata.data() + p[6];
				memcpy(m->vertices.data(), verts, 4 * m->vertices.size());
				memcpy(m->terms.data(), pdat->maindata.data() + p[8], 4 * m->terms.size());
				m->ftxo = p[9];
				m->weird = p[14];
			}
			o->line = lineIt->second;
		}

		if (o->flags & 0x0080)
		{
			o->light = std::make_shared<Light>();
			for (int i = 0; i < 7; i++)
				o->light->param[i] = p[6 + i];
		}

		uint8_t* dpbeg = pdbl->maindata.data() + p[0];
		o->dbl.load(dpbeg, idobjmap);

		uint32_t pexcoff = p[1];
		if (pexcoff != 0) {
			o->excChunk = std::make_shared<Chunk>();
			o->excChunk->load(pexc->maindata.data() + pexcoff - 1);
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

	// Audio objects
	Chunk* ands = spkchk.findSubchunk('SDNA');
	Chunk* sndr = spkchk.findSubchunk('RDNS');
	assert(ands && sndr);
	audioMgr.load(*ands, *sndr);

	// ZDefines
	Chunk* zdef = spkchk.findSubchunk('FEDZ');
	assert(zdef);
	zdefNames = (const char*)zdef->multidata[0].data();
	zdefValues.load(zdef->multidata[1].data(), idobjmap);
	zdefTypes = (const char*)zdef->multidata[2].data();

	// Messages
	Chunk* msgv = spkchk.findSubchunk('VGSM');
	for (Chunk& msg : msgv->subchunks) {
		uint32_t id = msg.tag;
		msgDefinitions[id] = std::make_pair((char*)msg.multidata[0].data(), (char*)msg.multidata[1].data());
	}

	// Texture to material assignment map
	Chunk* matl = spkchk.findSubchunk('LTAM');
	assert(matl);
	Chunk* mtlv = matl->findSubchunk('VLTM');
	assert(mtlv && mtlv->maindata.size() == 4 && *(uint32_t*)mtlv->maindata.data() == 1);
	for (size_t i = 0; i < matl->multidata.size(); i += 3) {
		textureMaterialMap.emplace_back((char*)matl->multidata[i].data(), (char*)matl->multidata[i + 1].data(), *(uint32_t*)matl->multidata[i + 2].data());
	}

	// Texture info (last ID)
	Chunk* ptxi = spkchk.findSubchunk('IXTP');
	numTextures = *(uint32_t*)ptxi->maindata.data();

	ready = true;
}

struct DBLHash
{
	size_t operator() (const std::pair<uint32_t, std::string>& e) const
	{
		const std::hash<std::string> h;
		return e.first + h(e.second);
	}
};

template<typename Unit, uint32_t OffsetUnit, bool IncludeStringNullTerminator = false>
struct PackBuffer {
	using Elem = typename Unit::value_type;

	std::vector<uint8_t> buffer;
	std::map<Unit, uint32_t> offmap;

	[[nodiscard]] uint32_t addByteOffset(const Unit& elem) {
		auto [it, inserted] = offmap.try_emplace(elem, (uint32_t)buffer.size());
		if (inserted) {
			uint8_t* ptr = (uint8_t*)std::data(elem);
			size_t len = sizeof(Elem) * (std::size(elem) + (IncludeStringNullTerminator ? 1 : 0));
			buffer.insert(buffer.end(), ptr, ptr + len);
		}
		return it->second;
	}
	[[nodiscard]] uint32_t add(const Unit& elem) {
		return addByteOffset(elem) / OffsetUnit;
	}
};

// Struct with all variables used when saving a Scene.
struct SceneSaver {
	uint32_t moc_objcount;
	std::map<GameObject*, uint32_t> objidmap;

	ByteWriter<std::vector<uint8_t>> heabuf;
	PackBuffer<std::array<float, 3>, 1> posPackBuf;
	PackBuffer<std::array<uint32_t, 4>, 16> mtxPackBuf;
	PackBuffer<std::string, 1, true> namPackBuf;
	PackBuffer<std::string, 1> dblPackBuf;
	PackBuffer<std::vector<float>, 4> verPackBuf;
	PackBuffer<std::vector<uint16_t>, 2> facPackBuf;
	PackBuffer<std::string, 1> datPackBuf;
	PackBuffer<std::string, 1> ftxPackBuf;
	PackBuffer<std::vector<float>, 4> uvcPackBuf;
	PackBuffer<std::string, 1> excPackBuf;

	void MakeObjChunk(Chunk* c, GameObject* o, bool isclp)
	{
		moc_objcount++;
		*c = {};

		// Position
		Vector3 position = o->matrix.getTranslationVector();
		std::array<float, 3> cpos = { position.x, position.y, position.z };
		uint32_t posoff = posPackBuf.add(cpos);

		// Matrix
		std::array<uint32_t, 4> cmtx;
		cmtx[0] = (uint32_t)(o->matrix._31 * 1073741824.0f) & ~1u; // multiply by 2^30
		cmtx[1] = (uint32_t)(o->matrix._32 * 1073741824.0f);
		if (o->matrix._33 < 0) cmtx[0] |= 1;
		cmtx[2] = (uint32_t)(o->matrix._21 * 1073741824.0f) & ~1u;
		cmtx[3] = (uint32_t)(o->matrix._22 * 1073741824.0f);
		if (o->matrix._23 < 0) cmtx[2] |= 1;
		uint32_t mtxoff = mtxPackBuf.add(cmtx);

		// DBL
		std::string dblsav = o->dbl.save(*this);
		uint32_t dbloff = dblPackBuf.add(dblsav);

		// Name
		uint32_t namoff = namPackBuf.add(o->name);

		// Vertices (Mesh+Line)
		uint32_t veroff = 0, trifacoff = 0, quadfacoff = 0, linetermoff = 0, ftxoff = 0;
		assert(!(o->mesh && o->line));
		if (o->mesh || o->line) {
			const auto& vertices = o->mesh ? o->mesh->vertices : o->line->vertices;
			if (!vertices.empty()) {
				veroff = verPackBuf.add(vertices);
			}
		}

		// Mesh
		if (o->mesh) {
			if (!o->mesh->triindices.empty()) {
				trifacoff = facPackBuf.add(o->mesh->triindices);
			}
			if (!o->mesh->quadindices.empty()) {
				quadfacoff = facPackBuf.add(o->mesh->quadindices);
			}
			uint32_t realftxoff = 0;
			if (!o->mesh->ftxFaces.empty()) {
				uint32_t tcOff = 0, lcOff = 0;
				if (!o->mesh->textureCoords.empty()) {
					tcOff = uvcPackBuf.add(o->mesh->textureCoords);
				}
				if (!o->mesh->lightCoords.empty()) {
					lcOff = uvcPackBuf.add(o->mesh->lightCoords);
				}
				ByteWriter<std::string> sb;
				uint32_t numFaces = (uint32_t)o->mesh->ftxFaces.size();
				std::array<uint32_t, 3> header = { tcOff, lcOff, numFaces };
				sb.addData(header.data(), 12);
				sb.addData(o->mesh->ftxFaces.data(), numFaces * 12);
				realftxoff = ftxPackBuf.add(sb.take()) + 1;
			}
			if (o->mesh->extension) {
				ByteWriter<std::string> sb;
				uint32_t numFrames = o->mesh->extension->frames.size();
				sb.addU32(numFrames);
				for (auto& [p1, p2] : o->mesh->extension->frames) {
					sb.addU32(p1);
					sb.addU32(p2);
				}
				sb.addStringNT(o->mesh->extension->name);
				uint32_t ext2off = datPackBuf.add(sb.take());
				std::array<uint32_t, 3> ext1 = { realftxoff, o->mesh->extension->extUnk2, ext2off };
				uint32_t ext1off = datPackBuf.add(std::string{ (char*)ext1.data(), 12 });
				ftxoff = ext1off | 0x80000000;
			}
			else {
				ftxoff = realftxoff;
			}
		}

		// Line
		if (o->line) {
			if (!o->line->terms.empty()) {
				const char* ptr = (const char*)o->line->terms.data();
				linetermoff = datPackBuf.add(std::string{ ptr, 4 * o->line->terms.size() });
			}
		}

		// EXC
		uint32_t pexcoff = 0;
		if (o->excChunk) {
			pexcoff = excPackBuf.add(o->excChunk->saveToString()) + 1;
		}

		// Object Header
		uint32_t heaoff = (uint32_t)heabuf.size();
		heabuf.addU32(dbloff);
		heabuf.addU32(pexcoff);
		heabuf.addU32(namoff);
		heabuf.addU32(mtxoff);
		heabuf.addU32(posoff);
		heabuf.addU16(o->type);
		heabuf.addU16(o->flags);
		if (o->flags & 0x0020)
		{
			assert(o->mesh);
			heabuf.addU32(veroff);
			heabuf.addU32(quadfacoff);
			heabuf.addU32(trifacoff);
			heabuf.addU32(ftxoff);
			uint32_t versize = o->mesh->getNumVertices();
			uint32_t quadsize = o->mesh->getNumQuads();
			uint32_t trisize = o->mesh->getNumTris();
			heabuf.addU32(versize);
			heabuf.addU32(quadsize);
			heabuf.addU32(trisize);
			heabuf.addU32(o->color);
			heabuf.addU32(o->mesh->weird);
		}
		if (o->flags & 0x0400)
		{
			assert(o->line);
			uint32_t zero = 0;
			heabuf.addU32(veroff);
			heabuf.addU32(zero);
			heabuf.addU32(linetermoff);
			heabuf.addU32(o->line->ftxo);
			uint32_t versize = o->line->getNumVertices();
			uint32_t termsize = o->line->terms.size();
			heabuf.addU32(versize);
			heabuf.addU32(zero);
			heabuf.addU32(termsize);
			heabuf.addU32(o->color);
			heabuf.addU32(o->line->weird);
		}
		if (o->flags & 0x0080)
		{
			assert(o->light);
			for (int i = 0; i < 7; i++)
				heabuf.addU32(o->light->param[i]);
		}

		// Object Chunk
		uint32_t tagstate = (o->isIncludedScene ? 2 : 0) | (isclp ? 1 : 0);
		c->tag = heaoff | (tagstate << 24);
		c->subchunks.resize(o->subobj.size());
		int i = 0;
		for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
		{
			Chunk* s = &c->subchunks[i];
			s->tag = 0;
			MakeObjChunk(s, *e, isclp);
			i++;
		}
	}
};

void Scene::ModifySPK()
{
	SceneSaver saver;

	Chunk nrot, nclp;
	nrot.tag = 'TORP';
	nclp.tag = 'PLCP';

	uint32_t objid = 1;
	auto z = [&objid,&saver](GameObject *o, auto& rec) -> void {
		for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
		{
			saver.objidmap[*e] = objid++;
			rec(*e, rec);
		}
	};
	z(cliprootobj, z);
	z(rootobj, z);

	auto f = [this,&saver](Chunk *c, GameObject *o) {
		c->subchunks.resize(o->subobj.size());
		saver.moc_objcount = 0;
		int i = 0;
		for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
		{
			Chunk *s = &c->subchunks[i++];
			saver.MakeObjChunk(s, *e, o==cliprootobj);
		}
		c->maindata.resize(4);
		*(uint32_t*)c->maindata.data() = saver.moc_objcount;
	};
	f(&nrot, rootobj);
	f(&nclp, cliprootobj);

	Chunk* prot = spkchk.findSubchunk('TORP');
	Chunk* pclp = spkchk.findSubchunk('PLCP');
	assert(prot && pclp);
	*prot = std::move(nrot);
	*pclp = std::move(nclp);

	// Fills a chunk
	auto fillMaindata = [](uint32_t tag, Chunk *nthg, const auto& buf) {
		using T = std::remove_reference_t<decltype(buf)>;
		nthg->tag = tag;
		nthg->maindata.resize(buf.size() * sizeof(T::value_type));
		memcpy(nthg->maindata.data(), buf.data(), nthg->maindata.size());
	};

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
			auto sum_a = ComputeBytesum(chka->maindata.data(), chka->maindata.size());
			auto sum_b = ComputeBytesum(chkb->maindata.data(), chkb->maindata.size());
			if (sum_a != sum_b)
				printf("Different bytesum\n");
			else
				printf("Same bytesum\n");
		}
		else if (chka->multidata.size()) {
			int numSizeDiff = 0, numContentDiff = 0, numSame = 0, numTotal = (int)chka->multidata.size();
			for (size_t i = 0; i < chka->multidata.size(); ++i) {
				if (chka->multidata[i].size() != chkb->multidata[i].size())
					numSizeDiff += 1;
				else if (memcmp(chka->multidata[i].data(), chkb->multidata[i].data(), chka->multidata[i].size()))
					numContentDiff += 1;
				else
					numSame += 1;
			}
			printf("%i/%i parts have different sizes\n", numSizeDiff, numTotal);
			printf("%i/%i parts have same size but content is different\n", numContentDiff, numTotal);
			printf("%i/%i parts have same size and content\n", numSame, numTotal);
		}
	};

	// Final move
	auto serveChunk = [&](const char* name, const auto& buffer) {
		Chunk* oldChunk = spkchk.findSubchunk(*(uint32_t*)name);
		assert(oldChunk != nullptr); // might as well create it if it didn't exist
		Chunk newChunk;
		fillMaindata(*(uint32_t*)name, &newChunk, buffer);
		chkcmp(oldChunk, &newChunk, name);
		*oldChunk = std::move(newChunk);
	};
	serveChunk("PHEA", saver.heabuf.take());
	serveChunk("PNAM", saver.namPackBuf.buffer);
	serveChunk("PPOS", saver.posPackBuf.buffer);
	serveChunk("PMTX", saver.mtxPackBuf.buffer);
	serveChunk("PDBL", saver.dblPackBuf.buffer);
	serveChunk("PVER", saver.verPackBuf.buffer);
	serveChunk("PFAC", saver.facPackBuf.buffer);
	serveChunk("PDAT", saver.datPackBuf.buffer);
	serveChunk("PFTX", saver.ftxPackBuf.buffer);
	serveChunk("PUVC", saver.uvcPackBuf.buffer);
	serveChunk("PEXC", saver.excPackBuf.buffer);

	((uint32_t*)spkchk.maindata.data())[1] = 0x40000;

	// Audio stuff
	Chunk* andsOld = spkchk.findSubchunk('SDNA');
	Chunk* sndrOld = spkchk.findSubchunk('RDNS');
	auto [andsNew, sndrNew] = audioMgr.save();
	chkcmp(andsOld, &andsNew, "ANDS");
	chkcmp(sndrOld, &sndrNew, "SNDR");
	*andsOld = std::move(andsNew);
	*sndrOld = std::move(sndrNew);

	// ZDefines
	Chunk* zdefOld = spkchk.findSubchunk('FEDZ');
	Chunk zdefNew('FEDZ');
	zdefNew.multidata.resize(3);
	auto strValues = zdefValues.save(saver);
	zdefNew.multidata[0].resize(zdefNames.size() + 1);
	zdefNew.multidata[1].resize(strValues.size());
	zdefNew.multidata[2].resize(zdefTypes.size() + 1);
	memcpy(zdefNew.multidata[0].data(), zdefNames.data(), zdefNew.multidata[0].size());
	memcpy(zdefNew.multidata[1].data(), strValues.data(), zdefNew.multidata[1].size());
	memcpy(zdefNew.multidata[2].data(), zdefTypes.data(), zdefNew.multidata[2].size());
	chkcmp(zdefOld, &zdefNew, "ZDEF");
	*zdefOld = std::move(zdefNew);

	// Messages
	Chunk* msgvOld = spkchk.findSubchunk('VGSM');
	Chunk msgvNew = Chunk('VGSM');
	msgvNew.subchunks.reserve(msgDefinitions.size());
	for (auto& [id, msg] : msgDefinitions) {
		Chunk& chk = msgvNew.subchunks.emplace_back(id);
		chk.multidata.resize(2);
		chk.multidata[0].resize(msg.first.size() + 1);
		chk.multidata[1].resize(msg.second.size() + 1);
		memcpy(chk.multidata[0].data(), msg.first.data(), chk.multidata[0].size());
		memcpy(chk.multidata[1].data(), msg.second.data(), chk.multidata[1].size());
	}
	chkcmp(msgvOld, &msgvNew, "MSGV");
	*msgvOld = std::move(msgvNew);

	// Texture to material assignment map
	Chunk* matlOld = spkchk.findSubchunk('LTAM');
	Chunk matlNew = Chunk('LTAM');
	Chunk& mtlvNew = matlNew.subchunks.emplace_back('VLTM');
	mtlvNew.maindata.resize(4);
	*(uint32_t*)mtlvNew.maindata.data() = 1;
	matlNew.multidata.resize(3 * textureMaterialMap.size());
	for (size_t i = 0; i < textureMaterialMap.size(); ++i) {
		auto& [texName, matName, num] = textureMaterialMap[i];
		matlNew.multidata[3 * i].resize(texName.size() + 1);
		matlNew.multidata[3 * i + 1].resize(matName.size() + 1);
		matlNew.multidata[3 * i + 2].resize(4);
		memcpy(matlNew.multidata[3 * i].data(), texName.data(), matlNew.multidata[3 * i].size());
		memcpy(matlNew.multidata[3 * i + 1].data(), matName.data(), matlNew.multidata[3 * i + 1].size());
		*(uint32_t*)matlNew.multidata[3 * i + 2].data() = num;
	}
	chkcmp(matlOld, &matlNew, "MATL");
	*matlOld = std::move(matlNew);

	// Texture info (last ID)
	Chunk* ptxi = spkchk.findSubchunk('IXTP');
	Chunk ptxiNew = Chunk('IXTP');
	ptxiNew.maindata.resize(4);
	*(uint32_t*)ptxiNew.maindata.data() = numTextures;
	*ptxi = std::move(ptxiNew);
}

void Scene::SaveSceneSPK(const char *fn)
{
	mz_zip_archive inzip, outzip;
	mz_zip_zero_struct(&inzip);
	mz_zip_zero_struct(&outzip);

	mz_bool mzr;
	mzr = mz_zip_reader_init_mem(&inzip, zipmem.data(), zipmem.size(), 0);
	if (!mzr) { warn("Couldn't reopen the original scene ZIP file."); return; }
	mzr = mz_zip_writer_init_file(&outzip, fn, 0);
	if (!mzr) { warn("Couldn't create the new scene ZIP file for saving."); return; }

	int nfiles = mz_zip_reader_get_num_files(&inzip);
	// Determine files to copy from original ZIP
	static constexpr const char* nocopyFiles[] = {"Pack.SPK", "Pack.PAL", "Pack.DXT", "Pack.ANM", "Pack.WAV", "Pack.LGT",
		"PackRepeat.PAL", "PackRepeat.DXT", "PackRepeat.ANM", "PackRepeat.WAV" };
	std::vector<bool> allowCopy = std::vector<bool>(nfiles, true);
	for (size_t i = 0; i < std::size(nocopyFiles); ++i) {
		int x = mz_zip_reader_locate_file(&inzip, nocopyFiles[i], nullptr, 0);
		if (x != -1)
			allowCopy[x] = false;
	}
	// Copy the files
	for (int i = 0; i < nfiles; i++)
		if (allowCopy[i])
			mz_zip_writer_add_from_zip_reader(&outzip, &inzip, i);

	auto saveChunk = [&outzip](Chunk* chk, const char* filename) {
		auto str = chk->saveToString();
		mz_zip_writer_add_mem(&outzip, filename, str.data(), str.size(), MZ_DEFAULT_COMPRESSION);
	};
	ModifySPK();
	saveChunk(&spkchk, "Pack.SPK");
	saveChunk(&palPack, "Pack.PAL");
	saveChunk(&dxtPack, "Pack.DXT");
	saveChunk(&lgtPack, "Pack.LGT");
	saveChunk(&wavPack, "Pack.WAV");
	if (hasAnmPack)
		saveChunk(&anmPack, "Pack.ANM");

	mz_zip_writer_finalize_archive(&outzip);
	mz_zip_writer_end(&outzip);
	mz_zip_reader_end(&inzip);
}

void Scene::Close()
{
	if (!ready)
		return;
	auto destroyObj = [](GameObject* obj, const auto& rec) -> void {
		for (GameObject* sub : obj->subobj)
			rec(sub, rec);
		delete obj;
	};
	if (superroot)
		destroyObj(superroot, destroyObj);
	ready = false;
	*this = {}; // move a default-constructed scene

	// clean ref counts
	for (auto it = g_objRefCounts.begin(); it != g_objRefCounts.end();) {
		if (it->second == 0u)
			it = g_objRefCounts.erase(it);
		else
			++it;
	}
}

void Scene::RemoveObject(GameObject *o)
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

GameObject* Scene::DuplicateObject(GameObject *o, GameObject *parent)
{
	if (!parent) parent = rootobj;
	if (!o->parent) return 0;
	GameObject *d = new GameObject(*o);
	
	//d->refcount = 0;
	d->subobj.clear();
	d->parent = parent;
	parent->subobj.push_back(d);
	for (int i = 0; i < o->subobj.size(); i++)
		DuplicateObject(o->subobj[i], d);

	return d;
}

void Scene::GiveObject(GameObject *o, GameObject *t)
{
	if (o->parent)
	{
		auto &st = o->parent->subobj;
		auto it = std::find(st.begin(), st.end(), o);
		if (it != st.end())
			st.erase(it);
	}
	t->subobj.push_back(o);
	o->parent = t;
}

GameObject* Scene::CreateObject(int type, GameObject* parent)
{
	GameObject* obj = new GameObject();
	obj->type = type;
	obj->flags = ClassInfo::GetObjTypeCategory(type);
	
	//obj->parent = parent;
	GiveObject(obj, parent);
	obj->root = parent->root;

	if (obj->flags & 0x0020)
		obj->mesh = std::make_shared<Mesh>();
	if (obj->flags & 0x0080)
		obj->light = std::make_shared<Light>();
	if (obj->flags & 0x0400)
		obj->line = std::make_shared<ObjLine>();

	auto members = ClassInfo::GetMemberNames(obj);
	for (auto& mem : members) {
		auto& cm = mem.info;
		auto& defValue = cm->defaultValue;
		DBLEntry& de = obj->dbl.entries.emplace_back();
		if (cm->type == "DOUBLE") {
			de.type = 1;
			de.value.emplace<double>(defValue.empty() ? 0.0f : std::stod(defValue));
		}
		else if (cm->type == "FLOAT") {
			de.type = 2;
			de.value.emplace<float>(defValue.empty() ? 0.0f : std::stof(defValue));
		}
		else if (cm->type == "INT" || cm->type == "LONG" || cm->type == "BOOL" || cm->type == "COLOR") {
			uint32_t value;
			if (defValue.empty() || defValue == "false" || defValue == "FALSE")
				value = 0;
			else if (defValue == "true" || defValue == "TRUE")
				value = 1;
			else
				value = std::stoi(defValue);
			de.type = 3;
			de.value.emplace<uint32_t>(value);
		}
		else if (cm->type == "ENUM") {
			de.type = 3;
			de.value.emplace<uint32_t>(0);
		}
		else if (cm->type == "WINOBJTYPE") {
			de.type = 3;
			de.value.emplace<uint32_t>(0);
		}
		else if (cm->type == "CHAR*" || cm->type == "SUBPIC") {
			de.type = 4;
			de.value.emplace<std::string>(defValue);
		}
		else if (cm->type == "DATA" || cm->type == "TABLE") {
			de.type = 7;
			de.value.emplace<std::vector<uint8_t>>();
		}
		else if (cm->type == "ZGEOMREF") {
			de.type = 8;
			de.value.emplace<GORef>();
		}
		else if (cm->type == "ZGEOMREFTAB") {
			de.type = 9;
			de.value.emplace<std::vector<GORef>>();
		}
		else if (cm->type == "MSG") {
			de.type = 10;
			de.value.emplace<uint32_t>(0);
		}
		else if (cm->type == "SNDREF" || cm->type == "SNDSETREF") {
			de.type = 11;
			de.value.emplace<AudioRef>();
		}
		else if (cm->type == "SCRIPT") {
			de.type = 12;
			de.value.emplace<DBLList>();
		}
		else if (cm->type == "") {
			de.type = 6;
		}
		else {
			printf("What is %s?\n", cm->type.c_str());
		}
	}
	auto& last = obj->dbl.entries.emplace_back();
	last.type = 0x3F;
	last.flags = 0xC0;
	return obj;
}

const char * DBLEntry::getTypeName(int type)
{
	type &= 0x3F;
	static const char* names[] = { "0", "double", "float", "int", "char*", "5", "EndCpnt", "7", "ZGEOMREF", "ZGEOMREFTAB", "MSG", "SNDREF", "Script" };
	if (type == 0x3F)
		return "EndDBL";
	if ((size_t)type < std::size(names))
		return names[type];
	return "?";
}

void DBLList::load(uint8_t* dpbeg, const std::map<uint32_t, GameObject*>& idobjmap)
{
	auto decodeRef = [&idobjmap](uint32_t id) -> GameObject* {
		if (id != 0)
			return idobjmap.at(id);
		else
			return nullptr;
	};

	uint32_t ds = *(uint32_t*)dpbeg & 0xFFFFFF;
	flags = (*(uint32_t*)dpbeg >> 24) & 255;
	uint8_t* dp = dpbeg + 4;
	while (dp - dpbeg < ds)
	{
		DBLEntry& e = entries.emplace_back();
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
			e.value = *(uint32_t*)dp;
			dp += 4;
			break;
		case 4:
		case 5:
			e.value.emplace<std::string>((const char*)dp);
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
			e.value.emplace<GORef>(decodeRef(*(uint32_t*)dp));
			dp += 4;
			break;
		case 9: {
			uint32_t nobjs = (*(uint32_t*)dp - 4) / 4;
			std::vector<GORef>& objlist = e.value.emplace<std::vector<GORef>>();
			for (uint32_t i = 0; i < nobjs; i++)
				objlist.emplace_back(decodeRef(*(uint32_t*)(dp + 4 + 4 * i)));
			dp += *(uint32_t*)dp;
			break;
		}
		case 0xB: {
			AudioRef& aoref = e.value.emplace<AudioRef>();
			aoref.id = *(uint32_t*)dp;
			dp += 4;
			break;
		}
		case 0xC: {
			DBLList& sublist = e.value.emplace<DBLList>();
			uint32_t dblsize = *(uint32_t*)dp;
			sublist.load(dp, idobjmap);
			dp += dblsize;
			break;
		}
		case 0x3F:
			break;
		default:
			ferr("Unknown DBL entry type!");
		}
	}
}

std::string DBLList::save(SceneSaver& sceneSaver)
{
	ByteWriter<std::string> dblsav;
	dblsav.addU32(0);
	for (auto e = entries.begin(); e != entries.end(); e++)
	{
		uint8_t typ = e->type | e->flags;
		dblsav.addU8(typ);
		switch (e->type)
		{
		case 0:
			break;
		case 1:
			dblsav.addDouble(std::get<double>(e->value)); break;
		case 2:
			dblsav.addFloat(std::get<float>(e->value)); break;
		case 3:
		case 0xA:
			dblsav.addU32(std::get<uint32_t>(e->value)); break;
		case 4:
		case 5:
			dblsav.addStringNT(std::get<std::string>(e->value)); break;
		case 6:
		case 0x3f:
			break;
		case 7:
		{
			auto& vec = std::get<std::vector<uint8_t>>(e->value);
			dblsav.addU32((uint32_t)vec.size() + 4);
			dblsav.addData(vec.data(), vec.size());
			break;
		}
		case 8:
		{
			auto& obj = std::get<GORef>(e->value);
			uint32_t x = sceneSaver.objidmap[obj.get()];
			dblsav.addU32(x); break;
		}
		case 9:
		{
			auto& vec = std::get<std::vector<GORef>>(e->value);
			uint32_t siz = (uint32_t)vec.size() * 4 + 4;
			dblsav.addU32(siz);
			for (auto& obj : vec) {
				uint32_t x = sceneSaver.objidmap[obj.get()];
				dblsav.addU32(x);
			}
			break;
		}
		case 0xB:
			dblsav.addU32(std::get<AudioRef>(e->value).id); break;
		case 0xC:
		{
			auto& sublist = std::get<DBLList>(e->value);
			auto subdblsav = sublist.save(sceneSaver);
			dblsav.addData(subdblsav.data(), subdblsav.size());
			break;
		}
		}
	}
	std::string str = dblsav.take();
	*(uint32_t*)str.data() = (uint32_t)str.size() | (flags << 24);
	return str;
}

std::string GameObject::getPath() const
{
	std::string str = name;
	for (const GameObject* obj = parent; obj; obj = obj->parent)
		str = obj->name + '/' + std::move(str);
	return str;
}

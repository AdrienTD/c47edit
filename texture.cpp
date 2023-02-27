#include "texture.h"

#include <functional>
#include <map>

#include "global.h"
#include "chunk.h"
#include "gameobj.h"

#include "miniz.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <GL/GL.h>

std::map<uint32_t, void*> texmap;

void ReadTextures()
{
	for (size_t i = 0; i < g_palPack.subchunks.size(); i++)
	{
		Chunk *c = &g_palPack.subchunks[i];
		uint8_t *d = (uint8_t*)c->maindata.data();
		uint32_t texid = *(uint32_t*)d;
		uint32_t texh = *(uint16_t*)(d + 4);
		uint32_t texw = *(uint16_t*)(d + 6);
		uint32_t nmipmaps = *(uint16_t*)(d + 8);
		uint8_t *firstbmp = d + 20;
		while (*(firstbmp++));

		uint32_t pal[256];
		if (c->tag == 'PALN')
		{
			uint8_t *pnt = firstbmp;
			for (int m = 0; m < nmipmaps; m++)
				pnt += *(uint32_t*)pnt + 4;
			uint32_t npalentries = *(uint32_t*)pnt; pnt += 4;
			if (npalentries > 256) npalentries = 256;
			memcpy(pal, pnt, 4 * npalentries);
		}

		uint32_t gltex;
		glGenTextures(1, &gltex);
		glBindTexture(GL_TEXTURE_2D, gltex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		texmap[texid] = (void*)gltex;

		uint8_t *bmp = firstbmp;
		for (int m = 0; m < nmipmaps; m++)
		{
			uint32_t mmsize = *(uint32_t*)bmp; bmp += 4;
			if (c->tag == 'PALN')
			{
				uint32_t *pix32 = new uint32_t[mmsize];
				for (uint32_t p = 0; p < mmsize; p++)
					pix32[p] = pal[bmp[p]];
				glTexImage2D(GL_TEXTURE_2D, m, 4, texw >> m, texh >> m, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix32);
				delete[] pix32;
			}
			else if(c->tag == 'RGBA')
				glTexImage2D(GL_TEXTURE_2D, m, 4, texw >> m, texh >> m, 0, GL_RGBA, GL_UNSIGNED_BYTE, bmp);
			else
				ferr("Unknown texture format in Pack(Repeat).PAL.");
			bmp += mmsize;
		}
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}
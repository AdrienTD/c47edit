// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "global.h"
#include "texture.h"
#include <Windows.h>
#include <gl/GL.h>

#define glprocvalid(x) (! ( ((uintptr_t)x==-1) || (((uintptr_t)x >= 0) && ((uintptr_t)x <= 3)) ) )
typedef BOOL(APIENTRY *gli_wglSwapIntervalEXT)(int n);

HDC whdc; HGLRC glrc;
uint drawframes = 0;
extern HWND hWindow;
bool rendertextures = false;

void InitVideo()
{
	whdc = GetDC(hWindow);

	// Set the pixel format
	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR)); // Be sure that pfd is filled with 0.
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 24;
	pfd.cDepthBits = 32;
	pfd.dwLayerMask = PFD_MAIN_PLANE;
	int i = ChoosePixelFormat(whdc, &pfd);
	SetPixelFormat(whdc, i, &pfd);

	glrc = wglCreateContext(whdc);
	wglMakeCurrent(whdc, glrc);

	// Set VSYNC if enabled
	//if (VSYNCenabled)
	if(1)
	{
		gli_wglSwapIntervalEXT wglSwapIntervalEXT = (gli_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
		if (glprocvalid(wglSwapIntervalEXT))
		{
			if (!wglSwapIntervalEXT(1))
				printf("wglSwapIntervalEXT returned FALSE.\n");
		}
		else printf("wglSwapIntervalEXT unsupported.\n");
	}
}

void BeginDrawing()
{
	glViewport(0, 0, screen_width, screen_height);
}

void EndDrawing()
{
	SwapBuffers(whdc); drawframes++;
}

void Mesh::draw()
{
	if (!rendertextures)
	{
		glVertexPointer(3, GL_FLOAT, 6, (float*)pver->maindata + this->vertstart);
		glDrawElements(GL_QUADS, this->numquads * 4, GL_UNSIGNED_SHORT, (uint16_t*)pfac->maindata + this->quadstart);
		glDrawElements(GL_TRIANGLES, this->numtris * 3, GL_UNSIGNED_SHORT, (uint16_t*)pfac->maindata + this->tristart);
	}
	else
	{
		uint16_t *f = (uint16_t*)pfac->maindata;
		float *v = (float*)pver->maindata + this->vertstart;
		static const float defu[8] = { 0,0, 0,1, 1,1, 1,0 };
		static const uint32_t c[4] = { 0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFF0000FF };
		uint8_t *ftxpnt = (uint8_t*)pftx->maindata + this->ftxo - 1;
		uint16_t *x = (uint16_t*)(ftxpnt + 12);
		static const int uvit[4] = { 0,1,2,3 };

		float *u = (float*)defu;
		if (this->ftxo && !(this->ftxo & 0x80000000))
			u = (float*)puvc->maindata + *(uint32_t*)(ftxpnt);

		for (int i = 0; i < this->numtris; i++)
		{
			if (this->ftxo && !(this->ftxo & 0x80000000)) {
				auto t = texmap.find(x[2]);
				if (t != texmap.end())
					glBindTexture(GL_TEXTURE_2D, (GLuint)t->second);
				else
					goto next_tri; // glBindTexture(GL_TEXTURE_2D, 0);
			}
			glBegin(GL_TRIANGLES);
			for (int j = 0; j < 3; j++) {
				glTexCoord2fv((float*)u + uvit[j] * 2);
				//glColor4ubv((uint8_t*)(c + j));
				glVertex3fv(v + f[this->tristart + i * 3 + j] * 3 / 2);
			}
			glEnd();
		next_tri:
			x += 6;
			u += 8; // Ignore 4th UV.
		}
		for (int i = 0; i < this->numquads; i++)
		{
			if (this->ftxo && !(this->ftxo & 0x80000000)) {
				auto t = texmap.find(x[2]);
				if (t != texmap.end())
					glBindTexture(GL_TEXTURE_2D, (GLuint)t->second);
				else
					goto next_quad; // glBindTexture(GL_TEXTURE_2D, 0);
			}
			glBegin(GL_TRIANGLE_FAN);
			for (int j = 0; j < 4; j++) {
				glTexCoord2fv((float*)u + uvit[j] * 2);
				//glColor4ubv((uint8_t*)(c + j));
				glVertex3fv(v + f[this->quadstart + i * 4 + j] * 3 / 2);
			}
			glEnd();
		next_quad:
			x += 6;
			u += 8;
		}
	}
}

void BeginMeshDraw()
{
	if (!rendertextures) {
		glEnableClientState(GL_VERTEX_ARRAY);
		glDisable(GL_TEXTURE_2D);
	}
	else {
		glDisableClientState(GL_VERTEX_ARRAY);
		glEnable(GL_TEXTURE_2D);
	}
	glColor4f(1, 1, 1, 1);
}

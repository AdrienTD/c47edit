// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "global.h"
#include <Windows.h>
#include <gl/GL.h>

#define glprocvalid(x) (! ( ((uintptr_t)x==-1) || (((uintptr_t)x >= 0) && ((uintptr_t)x <= 3)) ) )
typedef BOOL(APIENTRY *gli_wglSwapIntervalEXT)(int n);

HDC whdc; HGLRC glrc;
uint drawframes = 0;
extern HWND hWindow;

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
	glVertexPointer(3, GL_FLOAT, 6, (float*)pver->maindata + this->vertstart);
	//for(int i = 0; i < this->numquads; i++)
	//	glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, (uint16_t*)pfac->maindata + this->quadstart + i*4);
	glDrawElements(GL_QUADS, this->numquads * 4, GL_UNSIGNED_SHORT, (uint16_t*)pfac->maindata + this->quadstart);
	glDrawElements(GL_TRIANGLES, this->numtris*3, GL_UNSIGNED_SHORT, (uint16_t*)pfac->maindata + this->tristart);
}

void BeginMeshDraw()
{
	glEnableClientState(GL_VERTEX_ARRAY);
}

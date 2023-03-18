// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "window.h"
#include "global.h"
#include "video.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"

const char *apptitle = "c47edit", *appclassname = "AG_c47editWinClass";
HWND hWindow = NULL;
int screen_width = 1280, screen_height = 660;
bool win_minimized = false;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return TRUE;
	switch (uMsg)
	{
	case WM_SIZE:
	{
		int w = LOWORD(lParam);
		int h = HIWORD(lParam);
		if (wParam == SIZE_MINIMIZED || !w || !h) {
			win_minimized = true;
		}
		else {
			win_minimized = false;
			screen_width = w;
			screen_height = h;
		}
		break;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
	default:
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

void InitWindow()
{
	HINSTANCE hInstance = GetModuleHandle(NULL);
	WNDCLASS wndclass = { CS_OWNDC, WndProc, 0, 0, hInstance,
		LoadIconA(hInstance, "IDI_APPICON"), LoadCursor(NULL, IDC_ARROW), NULL, NULL, appclassname};
	if (!RegisterClass(&wndclass)) ferr("Class registration failed.");
	if (GetSystemMetrics(SM_CYFULLSCREEN) >= 900) { screen_width = 1422; screen_height = 800; }
	RECT rect = { 0,0,screen_width,screen_height };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	hWindow = CreateWindow(appclassname, apptitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right-rect.left, rect.bottom-rect.top, NULL, NULL, hInstance, NULL);
	if (!hWindow) ferr("Window creation failed.");
	ShowWindow(hWindow, SW_NORMAL);

	InitVideo();
}

bool HandleWindow()
{
	MSG msg;
	//if (winMinimized) WaitMessage();
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT) return false;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return true;
}
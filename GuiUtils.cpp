#include "GuiUtils.h"
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>

extern HWND hWindow;

static std::wstring simpleCharToWcharConvert(const char* str) {
	if (!str) return {};
	size_t len = strlen(str);
	return { str, str + len };
}

static std::wstring filterCharToWcharConvert(const char* str) {
	if (!str) return {};
	const char* ptr = str;
	// find a double null char
	while (*ptr || *(ptr + 1)) ptr++;
	return std::wstring{ str, ptr + 1 }.append({ 0,0 });
}

std::filesystem::path OpenDialogBox(const char* filter, const char* defExt, const char* title)
{
	wchar_t filepath[MAX_PATH + 1] = L"\0";
	OPENFILENAMEW ofn = {};
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWindow;
	ofn.hInstance = GetModuleHandle(NULL);
	std::wstring wFilter = filterCharToWcharConvert(filter);
	ofn.lpstrFilter = wFilter.c_str();
	ofn.nFilterIndex = 0;
	ofn.lpstrFile = filepath;
	ofn.nMaxFile = std::size(filepath) - 1;
	std::wstring wTitle;
	if (title) {
		wTitle = simpleCharToWcharConvert(title);
		ofn.lpstrTitle = wTitle.c_str();
	}
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	std::wstring wExt = simpleCharToWcharConvert(defExt);
	ofn.lpstrDefExt = wExt.c_str();
	if (GetOpenFileNameW(&ofn))
		return filepath;
	return {};
}

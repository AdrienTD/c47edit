#include "GuiUtils.h"
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

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

std::filesystem::path GuiUtils::OpenDialogBox(const char* filter, const char* defExt, const char* title)
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
	ofn.nMaxFile = std::size(filepath);
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

std::vector<std::filesystem::path> GuiUtils::MultiOpenDialogBox(const char* filter, const char* defExt)
{
	wchar_t filepath[1025] = L"\0";
	OPENFILENAMEW ofn = {};
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWindow;
	ofn.hInstance = GetModuleHandle(NULL);
	std::wstring wFilter = filterCharToWcharConvert(filter);
	ofn.lpstrFilter = wFilter.c_str();
	ofn.nFilterIndex = 0;
	ofn.lpstrFile = filepath;
	ofn.nMaxFile = std::size(filepath);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
	std::wstring wExt = simpleCharToWcharConvert(defExt);
	ofn.lpstrDefExt = wExt.c_str();
	if (GetOpenFileNameW(&ofn)) {
		std::wstring folder = filepath;
		const wchar_t* nextfile = filepath + folder.size() + 1;
		if (*nextfile) {
			std::vector<std::filesystem::path> list;
			while (*nextfile) {
				size_t len = wcslen(nextfile);
				list.emplace_back((folder + L'\\').append(std::wstring_view(nextfile, len)));
				nextfile += len + 1;
			}
			return list;
		}
		else {
			return { std::move(folder) };
		}
	}
	return {};
}

std::filesystem::path GuiUtils::SaveDialogBox(const char* filter, const char* defExt, const std::filesystem::path& defName, const char* title)
{
	wchar_t filepath[MAX_PATH + 1] = L"\0";
	if (!defName.empty())
		wcscpy_s(filepath, defName.c_str());
	OPENFILENAMEW ofn = {};
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hWindow;
	ofn.hInstance = GetModuleHandle(NULL);
	std::wstring wFilter = filterCharToWcharConvert(filter);
	ofn.lpstrFilter = wFilter.c_str();
	ofn.nFilterIndex = 0;
	ofn.lpstrFile = filepath;
	ofn.nMaxFile = std::size(filepath);
	std::wstring wTitle;
	if (title) {
		wTitle = simpleCharToWcharConvert(title);
		ofn.lpstrTitle = wTitle.c_str();
	}
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	std::wstring wExt = simpleCharToWcharConvert(defExt);
	ofn.lpstrDefExt = wExt.c_str();
	if (GetSaveFileNameW(&ofn))
		return filepath;
	return {};
}

std::filesystem::path GuiUtils::SelectFolderDialogBox(const char* text)
{
	wchar_t dirname[MAX_PATH + 1];
	BROWSEINFOW bri;
	memset(&bri, 0, sizeof(bri));
	bri.hwndOwner = hWindow;
	bri.pszDisplayName = dirname;
	std::wstring wText(text, text + strlen(text));
	bri.lpszTitle = wText.c_str();
	bri.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS;
	PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bri);
	if (pid != NULL) {
		SHGetPathFromIDListW(pid, dirname);
		return dirname;
	}
	return {};
}

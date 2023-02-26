#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace GuiUtils {
	std::filesystem::path OpenDialogBox(const char* filter, const char* defExt, const char* title = nullptr);
	std::vector<std::filesystem::path> MultiOpenDialogBox(const char* filter, const char* defExt);
	std::filesystem::path SaveDialogBox(const char* filter, const char* defExt, const std::filesystem::path& defName = {}, const char* title = nullptr);
	std::filesystem::path SelectFolderDialogBox(const char* text = nullptr);
};
#pragma once

#include <string>

namespace std::filesystem {
	class path;
}

std::filesystem::path OpenDialogBox(const char* filter, const char* defExt, const char* title = nullptr);

#pragma once

#include <array>
#include <string_view>
#include <vector>
#include "vecmat.h"

namespace std::filesystem {
	class path;
}

struct StringSplitter {
	const char* currentChar;
	const char* endChar;
	std::string_view splitChars;

	StringSplitter(std::string_view splitChars, const char* firstChar, const char* lastChar)
		: currentChar(firstChar), endChar(lastChar), splitChars(splitChars) {}
	StringSplitter(std::string_view splitChars, std::string_view str)
		: currentChar(str.data()), endChar(str.data() + str.size()), splitChars(splitChars) {}

	std::string_view next();
	bool finished() const { return currentChar == endChar; }
	bool isSplitChar(char c) const { return splitChars.find(c) != splitChars.npos; }
};

struct ObjModel {
	std::vector<Vector3> vertices;
	std::vector<std::array<int, 3>> triangles;
	struct Group {
		std::string name;
		int start, end;
	};
	std::vector<Group> groups;

	void load(const std::filesystem::path& filename);
	ObjModel() {}
	ObjModel(const std::filesystem::path& filename) { load(filename); }
};
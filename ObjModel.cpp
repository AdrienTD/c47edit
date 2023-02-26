#include "ObjModel.h"
#include <charconv>
#include <memory>
#include <cassert>
#include <filesystem>

std::string_view StringSplitter::next()
{
	// skip split chars
	while (currentChar != endChar && isSplitChar(*currentChar))
		++currentChar;
	// find non-split char
	const char* startChar = currentChar;
	while (currentChar != endChar && !isSplitChar(*currentChar))
		++currentChar;
	const char* finishChar = currentChar;
	// skip split chars
	while (currentChar != endChar && isSplitChar(*currentChar))
		++currentChar;
	return std::string_view(startChar, finishChar - startChar);
}

void ObjModel::load(const std::filesystem::path& filename)
{
	FILE* file;
	_wfopen_s(&file, filename.c_str(), L"rb");
	assert(file);
	fseek(file, 0, SEEK_END);
	size_t filesize = ftell(file);
	fseek(file, 0, SEEK_SET);
	std::unique_ptr<char[]> buffer = std::make_unique<char[]>(filesize);
	fread(buffer.get(), filesize, 1, file);
	fclose(file);

	Group curGroup;
	curGroup.start = 0; curGroup.end = 0;
	auto flushGroup = [this,&curGroup]() {
		int numTris = (int)triangles.size();
		if (curGroup.start < numTris) {
			curGroup.end = numTris;
			groups.push_back(std::move(curGroup));
		}
	};

	StringSplitter lineSplitter{ "\r\n", buffer.get(), buffer.get() + filesize };
	while (!lineSplitter.finished()) {
		std::string_view line = lineSplitter.next();
		StringSplitter wordSplitter{ " \t", line };
		std::string_view verb = wordSplitter.next();
		if (verb == "v") {
			Vector3 vec;
			for (float& f : vec) {
				std::string_view s = wordSplitter.next();
				std::from_chars(s.data(), s.data() + s.size(), f);
			}
			vertices.push_back(vec);
		}
		else if (verb == "f") {
			std::array<int, 3> indices;
			static std::vector<int> posIndices;
			posIndices.clear();
			while (!wordSplitter.finished()) {
				std::string_view vertex = wordSplitter.next();
				StringSplitter vpSplitter{ "/", vertex };
				std::string_view vpPos = vpSplitter.next();
				int posIndex;
				auto fcr = std::from_chars(vpPos.data(), vpPos.data() + vpPos.size(), posIndex);
				assert(fcr.ec == std::errc{});
				posIndices.push_back(posIndex - 1);
			}
			for (size_t i = 2; i < posIndices.size(); ++i) {
				triangles.push_back({ posIndices[0], posIndices[i - 1], posIndices[i] });
			}
			posIndices.clear();
		}
		else if (verb == "o") {
			flushGroup();
			curGroup.start = (int)triangles.size();
			curGroup.name = wordSplitter.next();
		}
	}
	flushGroup();
}

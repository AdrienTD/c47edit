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
		else if (verb == "vt") {
			Vector3 vec;
			for (float& f : vec) {
				std::string_view s = wordSplitter.next();
				std::from_chars(s.data(), s.data() + s.size(), f);
			}
			texCoords.push_back(vec);
		}
		else if (verb == "f") {
			static std::vector<std::array<int, 3>> posIndices;
			posIndices.clear();
			while (!wordSplitter.finished()) {
				std::string_view vertex = wordSplitter.next();
				StringSplitter vpSplitter{ "/", vertex };
				std::string_view vpPos = vpSplitter.next();
				std::string_view vpTxc = vpSplitter.next();
				std::string_view vpNrm = vpSplitter.next();
				int posIndex = 0, txcIndex = 0, nrmIndex = 0;
				auto fcr = std::from_chars(vpPos.data(), vpPos.data() + vpPos.size(), posIndex);
				assert(fcr.ec == std::errc{});
				if (!vpTxc.empty()) {
					fcr = std::from_chars(vpTxc.data(), vpTxc.data() + vpTxc.size(), txcIndex);
					assert(fcr.ec == std::errc{});
				}
				if (!vpNrm.empty()) {
					fcr = std::from_chars(vpNrm.data(), vpNrm.data() + vpNrm.size(), nrmIndex);
					assert(fcr.ec == std::errc{});
				}
				posIndices.push_back({ posIndex - 1, txcIndex - 1, nrmIndex - 1 });
			}
			for (size_t i = 2; i < posIndices.size(); ++i) {
				triangles.push_back({ posIndices[0], posIndices[i - 1], posIndices[i] });
			}
			posIndices.clear();
		}
		else if (verb == "usemtl") {
			flushGroup();
			curGroup.start = (int)triangles.size();
			curGroup.name = wordSplitter.next();
		}
		else if (verb == "mtllib") {
			std::string_view lib = std::string_view(wordSplitter.currentChar, wordSplitter.endChar - wordSplitter.currentChar);
			loadMaterialLib(filename.parent_path() / lib);
		}
	}
	flushGroup();
}

void ObjModel::loadMaterialLib(const std::filesystem::path& filename)
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

	StringSplitter lineSplitter{ "\r\n", buffer.get(), buffer.get() + filesize };
	Material* mat = nullptr;
	while (!lineSplitter.finished()) {
		std::string_view line = lineSplitter.next();
		StringSplitter wordSplitter{ " \t", line };
		std::string_view verb = wordSplitter.next();
		if (verb == "newmtl") {
			auto name = wordSplitter.next();
			mat = &materials.try_emplace(std::string(name)).first->second;
		}
		else if (verb == "map_Kd") {
			auto texpathstr = std::string_view(wordSplitter.currentChar, wordSplitter.endChar - wordSplitter.currentChar);
			mat->map_Kd = std::filesystem::path(texpathstr).stem().string();
		}
	}
}

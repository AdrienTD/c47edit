// DEBUG Code
// To test editor and help in reverse engineering the game.
// Code might be deleted in the future when not needed anymore.

#include "debug.h"

#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gameobj.h"
#include "imgui/imgui.h"
#include "classInfo.h"

#include "ScriptParser.h"
#include <fmt/format.h>

// Keeps track of what bytes of a chunk (or any kind of data) has been covered.
// E.g. did we read the entire chunk, didn't we forget to look at some bytes?
struct DataCoverage {
	std::vector<uint8_t> counts;
	std::vector<uint8_t> contexts;
	void setSize(size_t s) {
		counts.assign(s, 0);
		contexts.assign(s, 0);
	}
	void cover(size_t begin, size_t length, uint8_t ctxFlag) {
		assert(begin + length <= counts.size());
		for (size_t b = begin; b < begin + length; ++b)
			if (counts[b] != 255)
				counts[b] += 1;
		for (size_t b = begin; b < begin + length; ++b)
			contexts[b] |= ctxFlag;
	}
	void print() {
		if (counts.empty()) return;
		size_t start = 0;
		std::pair<uint8_t, uint8_t> c = std::make_pair(counts[0], contexts[0]);
		for (size_t o = 1; o < counts.size(); ++o) {
			auto cn = std::make_pair(counts[o], contexts[o]);
			if (cn != c) {
				printf("%3i,%i: %08zX - %08zX\n", c.first, c.second, start, o);
				start = o;
				c = cn;
			}
		}
		printf("%3i,%i: %08zX - %08zX\n", c.first, c.second, start, counts.size());
	}
	void printStatus() {
		printf("Coverage ");
		if (std::find(counts.begin(), counts.end(), 0) == counts.end())
			printf("\x1B[92mCOMPLETE!\x1B[0m\n");
		else
			printf("\x1B[91mINCOMPLETE!\x1B[0m\n");
	}
};

void EnableVT100()
{
	static bool enabled = false;
	if (!enabled) {
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD mode = 0;
		GetConsoleMode(hOut, &mode);
		mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		SetConsoleMode(hOut, mode);
		enabled = true;
	}
}

void IGDebugMenus()
{
	if (ImGui::BeginMenu("Debug")) {
		if (ImGui::MenuItem("FTX Stats")) {
			uint16_t minI4 = 0xFFFF, maxI4 = 0;
			uint16_t minI5 = 0xFFFF, maxI5 = 0;
			auto walkObj = [&](GameObject* obj, auto& rec) -> void {
				if (obj->mesh) {
					for (auto& face : obj->mesh->ftxFaces) {
						face[0] &= ~0x0200u;
						if (face[0] & 0x80) {
							minI4 = std::min(minI4, face[4]);
							maxI4 = std::max(maxI4, face[4]);
							minI5 = std::min(minI5, face[5]);
							maxI5 = std::max(maxI5, face[5]);
						}
					}
				}
				for (auto* child : obj->subobj) {
					rec(child, rec);
				}
			};
			walkObj(g_scene.superroot, walkObj);
			printf("face[4] in [0x%04X, 0x%04X]\n", minI4, maxI4);
			printf("face[5] in [0x%04X, 0x%04X]\n", minI5, maxI5);
		}
		if (ImGui::MenuItem("Remove PSCR")) {
			auto& remchunks = g_scene.remainingChunks;
			for (auto it = remchunks.begin(); it != remchunks.end(); ++it) {
				if (it->tag == 'RCSP') {
					it->tag = 'ABCD';
					it->maindata.resize(0);
					printf("begone!\n");
					break;
				}
			}
		}
		if (ImGui::MenuItem("Object flags stats")) {
			auto walkObj = [](GameObject* obj, auto& rec) -> void {
				printf("%s;%u\n", ClassInfo::GetObjTypeString(obj->type), obj->flags);
				for (auto* child : obj->subobj) {
					rec(child, rec);
				}
			};
			walkObj(g_scene.superroot, walkObj);
		}
		if (ImGui::MenuItem("Delete face anims")) {
			auto walkObj = [](GameObject* obj, auto& rec) -> void {
				if (obj->excChunk) {
					auto& subchunks = obj->excChunk->subchunks;
					for (auto it = subchunks.begin(); it != subchunks.end(); ) {
						if (it->tag == 'HPMO') {
							it = subchunks.erase(it);
							printf("removed at %s\n", obj->getPath().c_str());
						}
						else {
							++it;
						}
					}
				}
				for (auto* child : obj->subobj) {
					rec(child, rec);
				}
			};
			walkObj(g_scene.superroot, walkObj);

		}
		if (ImGui::MenuItem("List Components")) {
			auto walkObj = [](GameObject* obj, auto& rec) -> void {
				if (!obj->dbl.entries.empty()) {
					const std::string& str = std::get<std::string>(obj->dbl.entries[0].value);
					if (!str.empty()) {
						printf("%s\n", obj->getPath().c_str());
						printf("  %s\n", str.c_str());
					}
				}
				for (auto* child : obj->subobj) {
					rec(child, rec);
				}
			};
			walkObj(g_scene.superroot, walkObj);
		}
		if (ImGui::MenuItem("Test ScriptParser")) {
			ScriptParser parser(g_scene);
			try {
				parser.parseFile("Scripts\\C0_Training\\Personel01.sdl");

				fmt::println("===== Type Alias Map =====");
				for (auto& [key, val] : parser.typeAliasMap) {
					fmt::println("{} -> {}", key, val);
				}

				fmt::println("===== Scripts =====");
				for (auto& [name, script] : parser.scripts) {
					fmt::println("  ----- {} -----", name);
					for (auto& var : script.importedProperties)
						fmt::println("    {} {} = {};", var.type, var.name, var.defaultValue);
				}

				fmt::println("===== Property list string =====");
				fmt::println("{}", parser.getNativeImportPropertyList(parser.lastScript));
			}
			catch (const ScriptParserError& error) {
				fmt::println("!! Script Parsing Error !!\n{}", error.message);
			}
		}
		ImGui::EndMenu();
	}
}

void IGDebugWindows()
{
}

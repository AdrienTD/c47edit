// DEBUG Code
// To test editor and help in reverse engineering the game.
// Code might be deleted in the future when not needed anymore.

#include "debug.h"

#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "gameobj.h"
#include "imgui/imgui.h"

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
				printf("%3i,%i: %08X - %08X\n", c.first, c.second, start, o);
				start = o;
				c = cn;
			}
		}
		printf("%3i,%i: %08X - %08X\n", c.first, c.second, start, counts.size());
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
		if (ImGui::MenuItem("UV Coverage")) {
			EnableVT100();
			DataCoverage uvcover;
			uvcover.setSize(g_scene.puvc->maindata.size());
			Chunk* pdat = g_scene.spkchk->findSubchunk('TADP');
			assert(pdat);
			auto walkObj = [&](GameObject* obj, auto& rec) -> void {
				if ((obj->flags & 0x20) && obj->mesh) {
					if (obj->mesh->ftxo) {
						uint32_t ftxo;
						if (obj->mesh->ftxo & 0x80000000) {
							uint32_t* dat = (uint32_t*)((uint8_t*)pdat->maindata.data() + (obj->mesh->ftxo & 0x7FFF'FFFF));
							ftxo = dat[0];
						}
						else
							ftxo = obj->mesh->ftxo & 0x7FFFFFFF;
						uint8_t* ftxpnt = (uint8_t*)g_scene.pftx->maindata.data() + ftxo - 1;
						uint16_t* ftxFace = (uint16_t*)(ftxpnt + 12);
						size_t numFaces = obj->mesh->getNumQuads() + obj->mesh->getNumTris();
						assert(*(uint32_t*)(ftxpnt + 8) == numFaces);
						size_t numTexturedFaces = 0;
						size_t numLitFaces = 0;
						for (size_t f = 0; f < numFaces; ++f) {
							uint16_t flags = ftxFace[0];
							if (flags & 0x20)
								numTexturedFaces += 1;
							if (flags & 0x80)
								numLitFaces += 1;
							ftxFace += 6;
						}
						uvcover.cover(*(uint32_t*)ftxpnt * sizeof(float), sizeof(float) * 2 * 4 * numTexturedFaces, 1);
						uvcover.cover(*(uint32_t*)(ftxpnt + 4) * sizeof(float), sizeof(float) * 2 * 4 * numLitFaces, 2);
					}
				}
				for (auto* child : obj->subobj) {
					rec(child, rec);
				}
			};
			walkObj(g_scene.superroot, walkObj);
			printf("---------------\n");
			uvcover.print();
			uvcover.printStatus();
		}
		if (ImGui::MenuItem("List all FTXO")) {
			EnableVT100();
			Chunk* pdat = g_scene.spkchk->findSubchunk('TADP');
			assert(pdat);
			auto walkObj = [&](GameObject* obj, const std::string& indent, auto& rec) -> void {
				if ((obj->flags & 0x20) && obj->mesh) {
					printf("%s%s: ", indent.c_str(), obj->name.c_str());
					if (obj->mesh->ftxo == 0) {
						printf("\x1B[91mNo FTXO (%08X)\x1B[0m\n", obj->mesh->ftxo);
					}
					else {
						uint32_t ftxo;
						if (obj->mesh->ftxo & 0x80000000) {
							uint32_t* dat = (uint32_t*)((uint8_t*)pdat->maindata.data() + (obj->mesh->ftxo & 0x7FFF'FFFF));
							ftxo = dat[0];
						}
						else {
							ftxo = obj->mesh->ftxo;
						}
						uint8_t* ftxpnt = (uint8_t*)g_scene.pftx->maindata.data() + ftxo - 1;
						printf("FTXO:%08X  UV1:%08X  UV2:%08X\n", obj->mesh->ftxo - 1, *(uint32_t*)ftxpnt, *(uint32_t*)(ftxpnt + 4));
					}
				}
				std::string subindent = indent + ' ';
				for (auto* child : obj->subobj) {
					rec(child, subindent, rec);
				}
			};
			walkObj(g_scene.superroot, "", walkObj);
			printf("---------------\n");
		}
		if (ImGui::MenuItem("PVER PFAC Coverage")) {
			EnableVT100();
			DataCoverage cver, cfac;
			cver.setSize(g_scene.pver->maindata.size());
			cfac.setSize(g_scene.pfac->maindata.size());
			auto walkObj = [&](GameObject* obj, auto& rec) -> void {
				if (obj->mesh) {
					if (obj->flags & 0x400) {
						//printf("--> %s\n    %i %i\n", obj->getPath().c_str(), obj->line->tristart, obj->line->quadstart);
						//assert(obj->line->quadstart == 0);
						//cver.cover(4 * obj->mesh->vertstart, 4 * 3 * obj->mesh->numverts, 32);
						// it seems tristart points to PDAT here
					}
					else if (obj->flags & 0x20) {
						//cver.cover(4 * obj->mesh->vertstart, 4 * 3 * obj->mesh->numverts, 16);
						//cfac.cover(2 * obj->mesh->tristart, 2 * 3 * obj->mesh->numtris, 1);
						//cfac.cover(2 * obj->mesh->quadstart, 2 * 4 * obj->mesh->numquads, 2);
					}
				}
				for (auto* child : obj->subobj) {
					rec(child, rec);
				}
			};
			walkObj(g_scene.superroot, walkObj);
			printf("--- PVER ------\n");
			cver.print();
			printf("--- PFAC ------\n");
			cfac.print();
			printf("---------------\n");
			printf("PVER ");
			cver.printStatus();
			printf("PFAC ");
			cfac.printStatus();
			printf("---------------\n");
		}
		if (ImGui::MenuItem("PDAT Coverage")) {
			EnableVT100();
			Chunk* pdat = g_scene.spkchk->findSubchunk('TADP');
			assert(pdat);
			DataCoverage cdat;
			cdat.setSize(pdat->maindata.size());
			auto walkObj = [&](GameObject* obj, auto& rec) -> void {
				if (obj->mesh) {
					if (obj->flags & 0x400) {
						//uint32_t odat = obj->line->tristart;
						//cdat.cover(odat, 4 * obj->line->numtris, 4);
					}
					else if (obj->flags & 0x20) {
						if (obj->mesh->ftxo & 0x80000000) {
							uint32_t odat1 = obj->mesh->ftxo & 0x7FFF'FFFF;
							uint32_t* dat1 = (uint32_t*)((uint8_t*)pdat->maindata.data() + odat1);

							uint32_t odat2 = dat1[2];
							uint8_t* dat2 = (uint8_t*)pdat->maindata.data() + odat2;
							uint8_t* ptr2 = dat2;
							uint32_t numDings = *(uint32_t*)ptr2; ptr2 += 4;
							ptr2 += numDings * 8;
							while (*ptr2++); // skip string

							cdat.cover(odat1, 12, 1);
							cdat.cover(odat2, ptr2 - dat2, 2);
						}
					}
				}
				for (auto* child : obj->subobj) {
					rec(child, rec);
				}
			};
			walkObj(g_scene.superroot, walkObj);
			printf("--- PDAT ------\n");
			cdat.print();
			cdat.printStatus();
			printf("---------------\n");
		}
		ImGui::EndMenu();
	}
}

void IGDebugWindows()
{
}

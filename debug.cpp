// DEBUG Code
// To test editor and help in reverse engineering the game.
// Code might be deleted in the future when not needed anymore.

#include "debug.h"

#include <set>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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
			auto& subchunks = g_scene.spkchk->subchunks;
			for (auto it = subchunks.begin(); it != subchunks.end(); ++it) {
				if (it->tag == 'RCSP') {
					it->tag = 'ABCD';
					it->maindata.resize(0);
					printf("begone!\n");
					break;
				}
			}
		}
		if (ImGui::MenuItem("A Pose")) {
#pragma pack(push, 1)
			struct BonePre {
				uint16_t parentIndex, flags;
				double stuff[7];
				char name[16];
			};
			static_assert(sizeof(BonePre) == 0x4C);
#pragma pack(pop)
			std::set<Mesh*> visitedMeshes;
			
			auto walkObj = [&](GameObject* obj, auto& rec) -> void {
				if (obj->mesh && obj->excChunk && !visitedMeshes.count(obj->mesh.get())) {
					visitedMeshes.insert(obj->mesh.get());
					auto& exchk = *obj->excChunk;
					if (Chunk* lche = exchk.findSubchunk('LCHE')) {
						printf("A-posing %s\n", obj->getPath().c_str());
						Chunk* hmtx = exchk.findSubchunk('HMTX');
						Chunk* hpre = exchk.findSubchunk('HPRE');
						Chunk* hpts = exchk.findSubchunk('HPTS');
						Chunk* hpvd = exchk.findSubchunk('HPVD');
						Chunk* vrmp = exchk.findSubchunk('VRMP');
						assert(hmtx && hpre && hpts && vrmp && hpvd);
						uint32_t numBones = *(uint32_t*)lche->maindata.data();
						uint32_t numUsedVertices = *(uint32_t*)(lche->maindata.data() + 12);
						assert(hmtx->multidata.size() == numBones);

						// dump faces
						if (false) {
							printf("face dump:\n");
							uint16_t minIndex = 65535;
							uint16_t maxIndex = 0;
							auto checkIndex = [&](uint16_t i) {
								assert((i & 1) == 0);
								uint16_t x = i >> 1;
								minIndex = std::min(minIndex, x);
								maxIndex = std::max(maxIndex, x);
								return x;
							};
							int faceIndex = 0;
							for (auto it = obj->mesh->triindices.begin(); it != obj->mesh->triindices.end(); it += 3) {
								printf("%4i: %i %i %i\n", faceIndex++, checkIndex(it[0]), checkIndex(it[1]), checkIndex(it[2]));
							}
							for (auto it = obj->mesh->quadindices.begin(); it != obj->mesh->quadindices.end(); it += 4) {
								printf("%4i: %i %i %i %i\n", faceIndex++, checkIndex(it[0]), checkIndex(it[1]), checkIndex(it[2]), checkIndex(it[3]));
							}
							printf("minIndex: %i\nmaxIndex: %i\n", minIndex, maxIndex);
						}

						// compute global matrix for each bone
						std::vector<std::pair<Matrix, std::string>> boneGlobal;
						boneGlobal.reserve(numBones);
						for (uint32_t i = 0; i < numBones; ++i) {
							int xxx = i;
							Matrix globalMtx = Matrix::getIdentity();
							std::string bonePath;
							while (xxx != 65535) {
								const BonePre* bone;
								if (hpre->maindata.size() > 0)
									bone = ((BonePre*)hpre->maindata.data()) + xxx;
								else
									bone = (BonePre*)hpre->multidata[0].data() + xxx;
								const double* dmtx = (double*)hmtx->multidata[xxx].data();
								Matrix boneMtx = Matrix::getIdentity();
								for (int row = 3; row >= 0; --row) {
									boneMtx.m[row][0] = (float)*(dmtx++);
									boneMtx.m[row][1] = (float)*(dmtx++);
									boneMtx.m[row][2] = (float)*(dmtx++);
								}
								globalMtx = globalMtx * boneMtx;
								bonePath += bone->name;
								bonePath += '/';
								xxx = bone->parentIndex;
							}
							boneGlobal.push_back({ globalMtx, std::move(bonePath) });
						}

						// working vector buffer
						std::vector<Vector3> workBuffer;
						workBuffer.resize(4155); // obj->mesh->getNumVertices()
						memcpy(workBuffer.data(), obj->mesh->vertices.data(), 12 * obj->mesh->getNumVertices());

						// transform each vertex with the global matrix of the corresponding bone
						const uint16_t* ptsRanges = (uint16_t*)hpts->maindata.data();
						for (uint32_t i = 0; i < numBones; ++i) {
							uint16_t startRange = (i == 0) ? 0 : ptsRanges[i - 1];
							uint16_t endRange = ptsRanges[i];
							printf("%5i - %5i: %s\n", startRange, endRange, boneGlobal[i].second.c_str());
							for (uint16_t vtx = startRange; vtx < endRange; ++vtx)
								workBuffer[vtx] = workBuffer[vtx].transform(boneGlobal[i].first);
						}

						// apply HPVD
						const uint32_t* pvd = (uint32_t*)hpvd->maindata.data();
						bool the_end = false;
						uint32_t pvdCount = 0;
						while (!the_end) {
							uint32_t segStartInt = *pvd;
							float segStartFloat = *(float*)(pvd + 1);
							pvd += 2;
							printf("S %i %f", segStartInt, segStartFloat);
							Vector3& usedVec = workBuffer[segStartInt];
							usedVec *= segStartFloat;
							while (true) {
								uint32_t pntInt = *pvd;
								uint32_t pntIndex = pntInt & 0x3FFFFFFF;
								float pntFloat = *(float*)(pvd + 1);
								pvd += 2;
								printf(" - %i %f", pntIndex, pntFloat);
								usedVec += workBuffer[pntIndex] * pntFloat;
								if (pntInt & 0x80000000) {
									if (pntInt & 0x40000000)
										the_end = true;
									break;
								}
							}
							printf("\n");
							pvdCount += 1;
						}
						printf("pvdCount %i\n", pvdCount);

						// apply VRMP
						const uint32_t* remap = (uint32_t*)vrmp->maindata.data();
						uint32_t numSwaps = *(remap++);
						printf("numSwaps %i\n", numSwaps);
						for (uint32_t i = 0; i < numSwaps; ++i) {
							uint32_t index1 = *(remap++);
							uint32_t index2 = *(remap++);
							printf("remap %i %i\n", index1, index2);
							assert((index1 % 3) == 0 && (index2 % 3) == 0);
							workBuffer[index1 / 3] = workBuffer[index2 / 3];
						}

						// copy
						//memcpy(obj->mesh->vertices.data(), workBuffer.data(), 12 * numUsedVertices);
						memcpy(obj->mesh->vertices.data(), workBuffer.data(), 4 * obj->mesh->vertices.size());

						// do some corruption
						//vrmp->maindata.resize(12);
						//*(uint32_t*)vrmp->maindata.data() = 1;
						//memset(vrmp->maindata.data() + 4, 0, vrmp->maindata.size() - 4);
						//for (size_t pnt = 4; pnt < hpvd->maindata.size(); pnt += 8)
						//	*(float*)(hpvd->maindata.data() + pnt) *= 0.9f;
					}
				}
				for (auto* child : obj->subobj) {
					rec(child, rec);
				}
			};
			walkObj(g_scene.superroot, walkObj);
		}
		ImGui::EndMenu();
	}
}

void IGDebugWindows()
{
}

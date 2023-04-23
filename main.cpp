// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#define _USE_MATH_DEFINES
#include <cmath>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include "chunk.h"
#include "classInfo.h"
#include "gameobj.h"
#include "global.h"
#include "texture.h"
#include "vecmat.h"
#include "video.h"
#include "window.h"
#include "ObjModel.h"
#include "GuiUtils.h"
#include "debug.h"
#include "ModelImporter.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl2.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/ImGuizmo.h"

#include <stb_image.h>
#include <stb_image_write.h>

GameObject* selobj = 0;
Vector3 campos(0, 0, -50), camori(0,0,0);
float camNearDist = 1.0f, camFarDist = 10000.0f;
float camspeed = 32;
bool wireframe = false;
bool findsel = false;
uint32_t framesincursec = 0, framespersec = 0, lastfpscheck;
Vector3 cursorpos(0, 0, 0);
bool renderExc = false;

enum class ObjVisibility {
	Default = 0,
	Show = 1,
	Hide = 2
};
std::unordered_map<GameObject*, ObjVisibility> objVisibilityMap;
bool showZGates = false, showZBounds = false;
bool showInvisibleObjects = false;

bool IsObjectVisible(GameObject* obj) {
	if (!(obj->flags & 0x20))
		return false;
	if (!showInvisibleObjects && std::get<uint32_t>(obj->dbl.entries.at(9).value) != 0)
		return false;
	if (!showZGates && obj->type == 21)
		return false;
	if (!showZBounds && obj->type == 28)
		return false;
	for (GameObject* par = obj; par != nullptr; par = par->parent) {
		auto it = objVisibilityMap.find(par);
		if (it != objVisibilityMap.end()) {
			if (it->second == ObjVisibility::Show)
				return true;
			if (it->second == ObjVisibility::Hide)
				return false;
		}
	}
	return false;
}

GameObject *bestpickobj = 0;
float bestpickdist;
Vector3 bestpickintersectionpnt(0, 0, 0);

bool wndShowTextures = false;
bool wndShowSounds = false;
bool wndShowAudioObjects = false;
bool wndShowZDefines = false;

extern HWND hWindow;

void ferr(const char *str)
{
	//printf("Error: %s\n", str);
	MessageBox(hWindow, str, "Fatal Error", 16);
	exit(-1);
}

void warn(const char *str)
{
	MessageBox(hWindow, str, "Warning", 48);
}

bool ObjInObj(GameObject *a, GameObject *b)
{
	GameObject *o = a;
	while (o = o->parent)
	{
		if (o == b)
			return true;
	}
	return false;
}

int IGStdStringInputCallback(ImGuiInputTextCallbackData* data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		std::string* str = (std::string*)data->UserData;
		str->resize(data->BufTextLen);
		data->Buf = (char*)str->data();
	}
	return 0;
}
bool IGStdStringInput(const char* label, std::string& str) {
	return ImGui::InputText(label, str.data(), str.capacity() + 1, ImGuiInputTextFlags_CallbackResize, IGStdStringInputCallback, &str);
}

void IGAudioRef(const char* name, AudioRef& ref)
{
	AudioObject* obj = g_scene.audioMgr.getObject(ref.id);
	std::string preview = std::to_string(ref.id);
	if (obj) {
		preview += ": ";
		preview += g_scene.audioMgr.audioNames[ref.id];
	}
	if (ImGui::BeginCombo(name, preview.c_str())) {
		if (ImGui::Selectable("/", ref.id == 0))
			ref.id = 0;
		for (size_t i = 0; i < g_scene.audioMgr.audioObjects.size(); ++i) {
			auto& objptr = g_scene.audioMgr.audioObjects[i];
			if (objptr) {
				auto& name = g_scene.audioMgr.audioNames[i];
				ImGui::PushID(i);
				if (ImGui::Selectable("##Sound", ref.id == (uint32_t)i)) {
					ref.id = (uint32_t)i;
				}
				ImGui::SameLine();
				ImGui::Text("%zu: %s", i, name.c_str());
				ImGui::PopID();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::BeginDragDropSource()) {
		ImGui::SetDragDropPayload("AudioRef", &ref.id, 4);
		ImGui::TextUnformatted(preview.c_str());
		ImGui::EndDragDropSource();
	}
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* pay = ImGui::AcceptDragDropPayload("AudioRef")) {
			uint32_t pid = *(uint32_t*)pay->Data;
			ref.id = pid;
		}
		ImGui::EndDragDropTarget();
	}
}

void IGMessageValue(const char* name, uint32_t& ref)
{
	auto getName = [](int id) -> std::string {
		auto it = g_scene.msgDefinitions.find(id);
		if (it != g_scene.msgDefinitions.end()) {
			return std::to_string(id) + ": " + it->second.first;
		}
		return "(empty)";
	};

	if (ImGui::BeginCombo(name, getName(ref).c_str())) {
		if (ImGui::Selectable("(empty)", ref == 0))
			ref = 0;
		for (auto& [id, msg] : g_scene.msgDefinitions) {
			if (ImGui::Selectable(getName(id).c_str(), ref == id))
				ref = id;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", msg.second.c_str());
		}
		ImGui::EndCombo();
	}
}

class c47editException : public std::runtime_error { using std::runtime_error::runtime_error; };

std::unique_ptr<Scene> ExtractSubscene(Scene& ogScene, GameObject* ogObject)
{
	auto subscenePtr = std::make_unique<Scene>();
	Scene& subscene = *subscenePtr;
	subscene.LoadEmpty();
	std::map<GameObject*, GameObject*> cloneMap;
	auto walkObj = [&cloneMap](GameObject* obj, GameObject* parent, auto& rec) -> void {
		GameObject* clone = new GameObject(*obj);
		clone->subobj.clear();
		clone->parent = parent;
		parent->subobj.push_back(clone);
		cloneMap[obj] = clone;
		for (GameObject* child : obj->subobj)
			rec(child, clone, rec);
	};
	walkObj(ogObject, subscene.rootobj, walkObj);

	if (!ogScene.lgtPack.subchunks.empty())
		subscene.lgtPack.subchunks.emplace_back(ogScene.lgtPack.subchunks[0]);
	std::map<int, int> textureMap;
	auto fixref = [&cloneMap](GORef& ref) {
		if (ref) {
			auto it = cloneMap.find(ref.get());
			if (it == cloneMap.end())
				throw c47editException("Reference to object outside of the subscene");
			ref = it->second;
		}
	};
	for (const auto& [obj, clone] : cloneMap) {
		for (auto& de : clone->dbl.entries) {
			if (de.type == 8)
				fixref(std::get<GORef>(de.value));
			else if (de.type == 9)
				for (auto& go : std::get<std::vector<GORef>>(de.value))
					fixref(go);
		}

		if (clone->mesh) {
			for (auto& face : clone->mesh->ftxFaces) {
				for (auto& [flag, index] : std::array<std::pair<int, int>, 2>{ { {0x20, 2}, { 0x80, 3 } }}) {
					if ((face[0] & flag) && !(face[index] & 0x8000)) {
						int ogTexId = face[index];
						auto it = textureMap.find(ogTexId);
						if (it != textureMap.end()) {
							face[index] = (uint16_t)it->second;
						}
						else {
							subscene.numTextures += 1;
							auto [ogPal, ogDxt] = FindTextureChunk(ogScene, ogTexId);
							if (ogDxt) {
								auto& texCopyPal = subscene.palPack.subchunks.emplace_back(*ogPal);
								auto& texCopyDxt = subscene.dxtPack.subchunks.emplace_back(*ogDxt);
								*(uint32_t*)texCopyPal.maindata.data() = subscene.numTextures;
								*(uint32_t*)texCopyDxt.maindata.data() = subscene.numTextures;
							}
							else {
								auto& texCopyLgt = subscene.lgtPack.subchunks.emplace_back(*ogPal);
								*(uint32_t*)texCopyLgt.maindata.data() = subscene.numTextures;
							}
							textureMap[ogTexId] = subscene.numTextures;
							face[index] = (uint16_t)subscene.numTextures;
						}
					}
				}
			}
		}
	}

	return subscenePtr;
}

void IGOTNode(GameObject *o)
{
	bool op, colorpushed = 0;
	if (o == g_scene.superroot)
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (findsel)
		if (ObjInObj(selobj, o))
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	auto visibilityIt = objVisibilityMap.find(o);
	if (visibilityIt != objVisibilityMap.end() && visibilityIt->second != ObjVisibility::Default) {
		colorpushed = 1;
		ImVec4 color = (visibilityIt->second == ObjVisibility::Show) ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0, 0, 1);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
	}
	op = ImGui::TreeNodeEx(o, (o->subobj.empty() ? ImGuiTreeNodeFlags_Leaf : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ((o == selobj) ? ImGuiTreeNodeFlags_Selected : 0), "%s::%s", ClassInfo::GetObjTypeString(o->type), o->name.c_str());
	if (colorpushed)
		ImGui::PopStyleColor();
	if (findsel)
		if (selobj == o)
			ImGui::SetScrollHereY();
	if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
		ImGuiIO& io = ImGui::GetIO();
		if (io.KeyShift)
			objVisibilityMap[o] = ObjVisibility(((int)objVisibilityMap[o] + 1) % 3);
		else
		{
			selobj = o;
			cursorpos = selobj->matrix.getTranslationVector();
		}
	}
	if (ImGui::IsItemActive())
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("GameObject", &o, sizeof(GameObject*));
			ImGui::Text("GameObject: %s", o->name.c_str());
			ImGui::EndDragDropSource();
		}
	ImGui::PushID(o);
	if (ImGui::BeginPopupContextItem("ObjectRightClickMenu", ImGuiPopupFlags_MouseButtonRight)) {
		auto it = objVisibilityMap.find(o);
		ObjVisibility vis = (it != objVisibilityMap.end()) ? it->second : ObjVisibility::Default;
		if (ImGui::MenuItem("Default", nullptr, vis == ObjVisibility::Default)) objVisibilityMap[o] = ObjVisibility::Default;
		if (ImGui::MenuItem("Show", nullptr, vis == ObjVisibility::Show)) objVisibilityMap[o] = ObjVisibility::Show;
		if (ImGui::MenuItem("Hide", nullptr, vis == ObjVisibility::Hide)) objVisibilityMap[o] = ObjVisibility::Hide;
		ImGui::Separator();
		if (ImGui::MenuItem("Extract subscene")) {
			std::string fname = o->name;
			for (char& c : fname)
				if (c == '/' || c == '\\')
					c = '!';
			auto fpath = GuiUtils::SaveDialogBox("Scene (*.zip)\0*.zip\0\0\0\0\0", "zip", fname.c_str());
			if (!fpath.empty()) {
				try {
					auto subscenePtr = ExtractSubscene(g_scene, o);
					subscenePtr->SaveSceneSPK(fpath.string().c_str());
				}
				catch (const std::exception& exc) {
					std::string msg = "Failed to extract subscene!\nReason: ";
					msg += exc.what();
					MessageBoxA(hWindow, msg.c_str(), "c47edit", 16);
				}
			}
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
	if(op)
	{
		for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
			IGOTNode(*e);
		ImGui::TreePop();
	}
}

void IGObjectTree()
{
	ImGui::SetNextWindowPos(ImVec2(3, 23), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(316, 632), ImGuiCond_FirstUseEver);
	ImGui::Begin("Scene graph", 0, ImGuiWindowFlags_HorizontalScrollbar);
	IGOTNode(g_scene.superroot);
	findsel = false;
	ImGui::End();
}

Vector3 GetYXZRotVecFromMatrix(Matrix *m)
{
	float b = atan2(m->_31, m->_33);
	float j = atan2(m->_12, m->_22);
	float a = asin(-m->_32);
	return Vector3(a, b, j);
}

constexpr uint32_t swap_rb(uint32_t a) { return (a & 0xFF00FF00) | ((a & 0xFF0000) >> 16) | ((a & 255) << 16); }

GameObject *objtogive = 0;
uint32_t curtexid = 0;

std::vector<uint32_t> UnsplitDblImage(GameObject* obj, void* data, int type, int width, int height, bool opacity)
{
	std::vector<uint32_t> unpacked(width * height, 0xFFFF00FF);
	uint8_t* ptr = (uint8_t*)data;
	auto read8 = [&ptr]() {uint8_t val = *(uint8_t*)ptr; ptr += 1; return val; };
	auto read16 = [&ptr]() {int16_t val = *(int16_t*)ptr; ptr += 2; return val; };
	auto read32 = [&ptr]() {int32_t val = *(int32_t*)ptr; ptr += 4; return val; };
	bool weird = false;
	int numQuads = read32();
	if (numQuads == 0x40000001) {
		numQuads = read32();
		weird = true;
	}
	numQuads &= 0xFFFFFF;
	int numVerts = read32();
	std::vector<std::array<int32_t, 4>> quadIndices;
	std::vector<std::array<int16_t, 2>> vertices;
	quadIndices.resize(numQuads);
	for (auto& qi : quadIndices)
		for (auto& c : qi)
			c = read32();
	vertices.resize(numVerts);
	for (auto& v : vertices)
		for (auto& c : v)
			c = read32();
	std::vector<uint32_t> palette;
	if (type >= 2 && type <= 4) {
		int numPaletteColors = read32();
		palette.resize(numPaletteColors);
		for (uint32_t& c : palette)
			c = swap_rb(read32());
	}
	for (int i = 0; i < numQuads; ++i) {
		auto& qi = quadIndices[i];
		auto [lowX, highX] = std::minmax({ vertices[qi[0]][0], vertices[qi[1]][0], vertices[qi[2]][0], vertices[qi[3]][0] });
		auto [lowY, highY] = std::minmax({ vertices[qi[0]][1], vertices[qi[1]][1], vertices[qi[2]][1], vertices[qi[3]][1] });
		if(weird)
			read32();
		int16_t dw = read16();
		int16_t dh = read16();
		int16_t s2 = read16();
		int16_t s3 = read16();
		int rw = (s2 >= 0) ? s2 : dw;
		int rh = (s3 >= 0) ? s3 : dh;
		if (type == 0) {
			for (int dy = 0; dy < dh; ++dy) {
				for (int dx = 0; dx < dw; ++dx) {
					uint32_t c = swap_rb(read32());
					if (dx < rw && dy < rh)
						unpacked[(lowY + dy) * width + (lowX + dx)] = c;
				}
			}
		}
		else if (type == 2) {
			for (int dy = 0; dy < dh; ++dy) {
				for (int dx = 0; dx < dw; ++dx) {
					uint32_t c = palette[read8()];
					if (dx < rw && dy < rh)
						unpacked[(lowY + dy) * width + (lowX + dx)] = c;
				}
			}
		}
		else if (type == 3) {
			for (int dy = 0; dy < dh; ++dy) {
				for (int dx = 0; dx < dw; ++dx) {
					uint32_t c = palette[read8()] & 0xFFFFFF;
					if (dx < rw && dy < rh)
						unpacked[(lowY + dy) * width + (lowX + dx)] = c;
				}
			}
			for (int dy = 0; dy < dh; ++dy) {
				for (int dx = 0; dx < dw; ++dx) {
					uint8_t val = read8();
					if (dx < rw && dy < rh)
						unpacked[(lowY + dy) * width + (lowX + dx)] |= (uint32_t)val << 24;
				}
			}
		}
		else if (type == 4) {
			for (int dy = 0; dy < dh; ++dy) {
				for (int dx = 0; dx < dw; dx += 2) {
					uint8_t byte = read8();
					if (dx < rw && dy < rh)
						unpacked[(lowY + dy) * width + (lowX + dx)] = palette[byte & 15];
					if (dx + 1 < rw && dy < rh)
						unpacked[(lowY + dy) * width + (lowX + dx + 1)] = palette[byte >> 4];
				}
			}
		}

	}
	if (opacity) {
		// inverse alpha
		for (auto& col : unpacked)
			col ^= 0xFF000000;
	}
	else {
		// force alpha to 255
		for (auto& col : unpacked)
			col = 0xFF000000 | (col & 0x00FFFFFF);
	}
	return unpacked;
}

std::vector<uint8_t> SplitDblImage(uint32_t* image, int width, int height) {
	// NOTE: Texture pieces can only have a max size of 256x256 pixels.
	// For D3D and OGL renderers, pieces can be non power of 2.
	// For Glide, they HAVE to be power of 2.
	// Hence:
	// This algorithm will split image in 256x256 textures, remaining are upped to power of 2.
	// Not exactly how the original dbl images were splitted by devs, but good enough.

	std::vector<uint8_t> buffer;
	auto writeAny = [&buffer](const auto& val) {uint8_t* ptr = (uint8_t*)&val; buffer.insert(buffer.end(), ptr, ptr + sizeof(val)); };
	auto write16 = [&](int16_t val) {writeAny(val); };
	auto write32 = [&](int32_t val) {writeAny(val); };
	auto bitceil = [](uint32_t val) {
		for (uint32_t t = 0x80000000; t != 0; t >>= 1) {
			if (t & val) {
				return (t == val) ? t : (t << 1);
			}
		}
		return 0u;
	};

	static constexpr int MAX_LENGTH = 256;
	int numQuadsX = width / MAX_LENGTH + ((width % MAX_LENGTH) ? 1 : 0);
	int numQuadsY = height / MAX_LENGTH + ((height % MAX_LENGTH) ? 1 : 0);
	int numQuads = numQuadsX * numQuadsY;
	int numVertices = (numQuadsX + 1) * (numQuadsY + 1);
	write32(numQuads);
	write32(numVertices);
	for (int y = 0; y < numQuadsY; ++y) {
		for (int x = 0; x < numQuadsX; ++x) {
			write32((y + 1) * (numQuadsX + 1) + x);
			write32((y + 1) * (numQuadsX + 1) + x + 1);
			write32(y * (numQuadsX + 1) + x + 1);
			write32(y * (numQuadsX + 1) + x);
		}
	}
	for (int y = 0; y < numQuadsY + 1; ++y) {
		for (int x = 0; x < numQuadsX + 1; ++x) {
			write32(std::min(x * MAX_LENGTH, width));
			write32(std::min(y * MAX_LENGTH, height));
		}
	}
	for (int y = 0; y < numQuadsY; ++y) {
		for (int x = 0; x < numQuadsX; ++x) {
			int px0 = x * MAX_LENGTH;
			int py0 = y * MAX_LENGTH;
			int px1 = std::min((x + 1) * MAX_LENGTH, width);
			int py1 = std::min((y + 1) * MAX_LENGTH, height);
			int sqwidth = px1 - px0;
			int sqheight = py1 - py0;
			int texwidth = bitceil(sqwidth);
			int texheight = bitceil(sqheight);
			write16(texwidth);
			write16(texheight);
			write16(sqwidth);
			write16(sqheight);
			for (int v = py0; v < py0+texheight; ++v) {
				for (int u = px0; u < px0+texwidth; ++u) {
					if (u < px1 && v < py1)
						write32(swap_rb(image[v * width + u]) ^ 0xFF000000);
					else
						write32(0);
				}
			}
		}
	}
	return buffer;
}

GLuint GetDblImageTexture(GameObject* obj, void* data, int type, int width, int height, bool opacity, bool refresh) {
	static GameObject* previousObj = nullptr;
	static GLuint tex = 0;
	if (!refresh && obj == previousObj)
		return tex;
	if (tex)
		glDeleteTextures(1, &tex);
	previousObj = obj;

	auto image = UnsplitDblImage(obj, data, type, width, height, opacity);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, 4, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
	return tex;
}

static GameObject* nextobjtosel = 0;

void IGDBLList(DBLList& dbl, const std::vector<ClassInfo::ObjectMember>& members)
{
	size_t memberIndex = 0;
	ImGui::InputScalar("DBL Flags", ImGuiDataType_U32, &dbl.flags);
	int i = 0;
	for (auto e = dbl.entries.begin(); e != dbl.entries.end(); e++)
	{
		static const ClassInfo::ClassMember oobClassMember = { "", "OOB" };
		static const ClassInfo::ObjectMember oobObjMember = { &oobClassMember };
		const auto& [mem, arrayIndex] = (memberIndex < members.size()) ? members[memberIndex++] : oobObjMember;
		std::string nameIndexed;
		if (arrayIndex != -1)
			nameIndexed = mem->name + '[' + std::to_string(arrayIndex) + ']';
		const std::string& name = (arrayIndex != -1) ? nameIndexed : mem->name;
		ImGui::PushID(i++);
		if (mem->isProtected)
			ImGui::BeginDisabled();
		ImGui::Text("%1X", e->flags >> 4);
		ImGui::SameLine();
		switch (e->type)
		{
		case 0:
			ImGui::Text("0"); break;
		case 1:
			ImGui::InputDouble(name.c_str(), &std::get<double>(e->value)); break;
		case 2:
			ImGui::InputFloat(name.c_str(), &std::get<float>(e->value)); break;
		case 3:
		{
			uint32_t& ref = std::get<uint32_t>(e->value);
			if (mem->type == "BOOL") {
				bool val = ref;
				if (ImGui::Checkbox(name.c_str(), &val))
					ref = val ? 1 : 0;
			}
			else if (mem->type == "ENUM") {
				if (ImGui::BeginCombo(name.c_str(), mem->valueChoices[ref].c_str())) {
					for (size_t i = 0; i < mem->valueChoices.size(); ++i)
						if (ImGui::Selectable(mem->valueChoices[i].c_str(), ref == (uint32_t)i))
							ref = (uint32_t)i;
					ImGui::EndCombo();
				}
			}
			else {
				ImGui::InputInt(name.c_str(), (int*)&ref);
			}
			break;
		}
		case 4:
		case 5:
		{
			auto& str = std::get<std::string>(e->value);
			//IGStdStringInput((e->type == 5) ? "Filename" : "String", str);
			IGStdStringInput(name.c_str(), str);
			break;
		}
		case 6:
			ImGui::Separator(); break;
		case 7: {
			auto& data = std::get<std::vector<uint8_t>>(e->value);
			ImGui::Text("Data (%s): %zu bytes", name.c_str(), data.size());
			ImGui::SameLine();
			if (ImGui::SmallButton("Export")) {
				FILE* file;
				fopen_s(&file, "c47data.bin", "wb");
				fwrite(data.data(), data.size(), 1, file);
				fclose(file);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Import")) {
				FILE* file;
				fopen_s(&file, "c47data.bin", "rb");
				fseek(file, 0, SEEK_END);
				auto len = ftell(file);
				fseek(file, 0, SEEK_SET);
				data.resize(len);
				fread(data.data(), data.size(), 1, file);
				fclose(file);
			}
			if (name == "Squares") {
				std::string& name = std::get<std::string>((e + 1)->value);
				uint32_t& width = std::get<uint32_t>((e + 2)->value);
				uint32_t& height = std::get<uint32_t>((e + 3)->value);
				uint32_t& picSplitX = std::get<uint32_t>((e + 4)->value);
				uint32_t& picSplitY = std::get<uint32_t>((e + 5)->value);
				uint32_t& opacity = std::get<uint32_t>((e + 6)->value);
				uint32_t& format = std::get<uint32_t>((e + 12)->value);
				uint32_t& picSize = std::get<uint32_t>((e + 13)->value);
				bool refresh = false;
				if (ImGui::Button("Import image")) {
					auto fpath = GuiUtils::OpenDialogBox("PNG Image\0*.png\0\0\0\0", "png");
					if (!fpath.empty()) {
						int impWidth, impHeight, impChannels;
						auto image = stbi_load(fpath.string().c_str(), &impWidth, &impHeight, &impChannels, 4);
						data = SplitDblImage((uint32_t*)image, impWidth, impHeight);
						width = impWidth;
						height = impHeight;
						picSplitX = 0;
						picSplitY = 0;
						format = 0;
						picSize = 0;
						stbi_image_free(image);
						refresh = true;
					}
				}
				if (!data.empty()) {
					ImGui::SameLine();
					if (ImGui::Button("Export image")) {
						auto fname = std::filesystem::path(name).stem().string() + ".png";
						auto fpath = GuiUtils::SaveDialogBox("PNG Image\0*.png\0\0\0\0", "png", fname.c_str());
						if (!fpath.empty()) {
							auto image = UnsplitDblImage(selobj, data.data(), format, width, height, opacity);
							stbi_write_png(fpath.string().c_str(), width, height, 4, image.data(), 0);
						}
					}
					GLuint tex = GetDblImageTexture(selobj, data.data(), format, width, height, opacity, refresh);
					int dispHeight = std::min(128u, height);
					int dispWidth = width * dispHeight / height;
					ImGui::Image((void*)(uintptr_t)tex, ImVec2((float)dispWidth, (float)dispHeight));
				}
			}
			break;
		}
		case 8:
			if (auto& obj = std::get<GORef>(e->value); obj.valid()) {
				ImGui::LabelText(name.c_str(), "Object %s::%s", ClassInfo::GetObjTypeString(obj->type), obj->name.c_str());
				if (ImGui::IsItemClicked())
					nextobjtosel = obj.get();
			}
			else
				ImGui::LabelText(name.c_str(), "Object <Invalid>");
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("GameObject"))
				{
					e->value.emplace<GORef>(*(GameObject**)pl->Data);
				}
				ImGui::EndDragDropTarget();
			}
			break;
		case 9: {
			auto& vec = std::get<std::vector<GORef>>(e->value);
			if (ImGui::BeginListBox("##Objlist", ImVec2(0, 64))) {
				for (auto& obj : vec)
				{
					ImGui::Text("%s", obj->name.c_str());
					if (ImGui::IsItemClicked())
						nextobjtosel = obj.get();
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("GameObject"))
						{
							obj = *(GameObject**)pl->Data;
						}
						ImGui::EndDragDropTarget();
					}
				}
				ImGui::EndListBox();
				ImGui::SameLine();
				ImGui::Text("%s\n%zu objects", name.c_str(), vec.size());
			}
			break;
		}
		case 0xA:
			IGMessageValue(name.c_str(), std::get<uint32_t>(e->value));
			break;
		case 0xB: {
			IGAudioRef(name.c_str(), std::get<AudioRef>(e->value));
			break;
		}
		case 0xC: {
			ImGui::Indent();
			static const ClassInfo::ClassMember scriptHeader[2] = { {"", "ScriptFile"}, {"", "ScriptMembers"} };
			static const std::vector<ClassInfo::ObjectMember> oScriptHeader = { {&scriptHeader[0]}, {&scriptHeader[1]} };
			IGDBLList(std::get<DBLList>(e->value), oScriptHeader);
			ImGui::Unindent();
			break;
		}
		case 0x3F:
			ImGui::Text("End"); break;
		default:
			ImGui::Text("Unknown type %u", e->type); break;
		}
		if (mem->isProtected)
			ImGui::EndDisabled();
		ImGui::PopID();
	}
}

void IGObjectInfo()
{
	nextobjtosel = 0;
	ImGui::SetNextWindowPos(ImVec2(965, 23), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(310, 425), ImGuiCond_FirstUseEver);
	ImGui::Begin("Object information");
	if (!selobj)
		ImGui::Text("No object selected.");
	else {
		if (ImGui::Button("Duplicate"))
			if(selobj->root)
				g_scene.DuplicateObject(selobj, selobj->root);
		ImGui::SameLine();
		bool wannadel = 0;
		if (ImGui::Button("Delete"))
			wannadel = 1;
		
		if (ImGui::Button("Set to be given"))
			objtogive = selobj;
		ImGui::SameLine();
		if (ImGui::Button("Give it here!"))
			if(objtogive)
				g_scene.GiveObject(objtogive, selobj);
		
		if (ImGui::Button("Find in graph"))
			findsel = true;
		if (selobj->parent)
		{
			ImGui::SameLine();
			if (ImGui::Button("Select parent"))
				selobj = selobj->parent;
		}
		ImGui::Separator();

		ImGui::Text("%s (%i, %04X) %s", ClassInfo::GetObjTypeString(selobj->type), selobj->type, selobj->flags, selobj->isIncludedScene ? "Included Scene" : "");
		IGStdStringInput("Name", selobj->name);
		ImGui::DragFloat3("Position", &selobj->matrix._41);
		/*for (int i = 0; i < 3; i++) {
			ImGui::PushID(i);
			ImGui::DragFloat3((i==0) ? "Matrix" : "", selobj->matrix.m[i]);
			ImGui::PopID();
		}*/
		Vector3 rota = GetYXZRotVecFromMatrix(&selobj->matrix);
		rota *= 180.0f * (float)M_1_PI;
		if (ImGui::DragFloat3("Orientation", &rota.x))
		{
			rota *= (float)M_PI / 180.0f;
			Matrix my = Matrix::getRotationYMatrix(rota.y);
			Matrix mx = Matrix::getRotationXMatrix(rota.x);
			Matrix mz = Matrix::getRotationZMatrix(rota.z);
			selobj->matrix = mz * mx * my;
		}
		ImGui::Text("Num. references: %zu", selobj->getRefCount());
		if (ImGui::CollapsingHeader("Properties (DBL)"))
		{
			if (ImGui::Button("Add routine")) {
				ImGui::OpenPopup("AddRoutineMenu");
			}
			if (ImGui::BeginPopup("AddRoutineMenu")) {
				for (auto& [name, memlist] : g_classMemberLists) {
					if (name.find('_') != name.npos) {
						if (ImGui::MenuItem(name.c_str())) {
							std::string& routstr = std::get<std::string>(selobj->dbl.entries[0].value);
							if (!routstr.empty())
								routstr += ',';
							routstr += name;
							routstr += " 0";
							std::vector<ClassInfo::ObjectMember> objmems;
							ClassInfo::AddDBLMemberInfo(objmems, memlist);
							selobj->dbl.addMembers(objmems);
						}
					}
				}
				ImGui::EndPopup();
			}
			auto members = ClassInfo::GetMemberNames(selobj);
			IGDBLList(selobj->dbl, members);
		}
		if (selobj->mesh && ImGui::CollapsingHeader("Mesh"))
		{
			static const char* modelFileFilter = "3D Model\0*.glb;*.gltf;*.dae;*.blend;*.obj;*.ogex;*.fbx\0"
				"glTF 2.0 Binary (*.glb)\0*.glb\0"
				"glTF 2.0 Text (*.gltf)\0*.gltf\0"
				"Collada (*.dae)\0*.dae\0"
				"OpenGEX (*.ogex)\0*.ogex\0"
				"Wavefront OBJ (*.obj)\0*.obj\0"
				"Blender (*.blend)\0*.blend\0"
				"FBX (*.fbx)\0*.fbx\0"
				"Any format\0*.*\0\0\0\0";
			if (ImGui::Button("Import")) {
				auto filepath = GuiUtils::OpenDialogBox(modelFileFilter, "glb");
				if (!filepath.empty()) {
					if (auto optMesh = ImportWithAssimp(filepath)) {
						if (!selobj->mesh)
							selobj->mesh = std::make_shared<Mesh>();
						*selobj->mesh = std::move(optMesh->first);
						if (optMesh->second) {
							selobj->excChunk = std::make_shared<Chunk>(std::move(*optMesh->second));
							// set exchunk to every other object sharing the same mesh
							auto walkObj = [](GameObject* obj, auto& rec) -> void {
								if (obj->mesh == selobj->mesh)
									obj->excChunk = std::make_shared<Chunk>(*selobj->excChunk);
								for (GameObject* child : obj->subobj)
									rec(child, rec);
							};
							walkObj(g_scene.superroot, walkObj);
						}
						//else
						//	selobj->excChunk = nullptr;
						InvalidateMesh(selobj->mesh.get());
						UncacheAllTextures();
						GlifyAllTextures();
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Export")) {
				auto filepath = GuiUtils::SaveDialogBox(modelFileFilter, "glb");
				if (!filepath.empty()) {
					ExportWithAssimp(*selobj->mesh, filepath, selobj->excChunk.get());
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Mesh tools")) {
				ImGui::OpenPopup("MeshTools");
			}
			if (ImGui::BeginPopup("MeshTools")) {
				static Vector3 scale{ 1.0f, 1.0f, 1.0f };
				static bool doScale = false;
				static bool invertFaces = false;
				ImGui::Checkbox("Scale:", &doScale);
				ImGui::BeginDisabled(!doScale);
				ImGui::InputFloat3("Factor", &scale.x);
				ImGui::EndDisabled();
				ImGui::Checkbox("Invert faces", &invertFaces);
				if (ImGui::Button("Apply")) {
					Mesh* mesh = selobj->mesh.get();
					if (doScale) {
						float* verts = mesh->vertices.data();
						for (size_t i = 0; i < mesh->vertices.size(); i += 3) {
							verts[i] *= scale.x;
							verts[i + 1] *= scale.y;
							verts[i + 2] *= scale.z;
						}
					}
					if (invertFaces) {
						uint16_t* triIndices = mesh->triindices.data();
						uint16_t* quadIndices = mesh->quadindices.data();
						bool hasFtx = !selobj->mesh->ftxFaces.empty();
						assert(hasFtx);
						float* uvCoords = (float*)selobj->mesh->textureCoords.data();
						using UVQuad = std::array<std::array<float, 2>, 4>;
						static_assert(sizeof(UVQuad) == 4 * 8);
						UVQuad* uvQuads = (UVQuad*)uvCoords;
						uint16_t* ftxFace = (uint16_t*)selobj->mesh->ftxFaces.data();
						for (uint32_t i = 0; i < mesh->getNumTris(); ++i) {
							std::swap(triIndices[0], triIndices[2]);
							UVQuad& q = *uvQuads;
							std::swap(q[0], q[2]);
							triIndices += 3;
							uvQuads += 1;
						}
						for (uint32_t i = 0; i < mesh->getNumQuads(); ++i) {
							std::swap(quadIndices[0], quadIndices[3]);
							std::swap(quadIndices[1], quadIndices[2]);
							UVQuad& q = *uvQuads;
							std::swap(q[0], q[3]);
							std::swap(q[1], q[2]);
							quadIndices += 4;
							uvQuads += 1;
						}
					}
					InvalidateMesh(mesh);
				}
				ImGui::EndPopup();
			}

			ImGui::AlignTextToFramePadding();
			ImGui::Text("Ref count: %li", selobj->mesh.use_count());
			ImGui::SameLine();
			if (ImGui::Button("Make unique"))
				selobj->mesh = std::make_unique<Mesh>(*selobj->mesh);
			ImVec4 c = ImGui::ColorConvertU32ToFloat4(swap_rb(selobj->color));
			if (ImGui::ColorEdit4("Color", &c.x, 0))
				selobj->color = swap_rb(ImGui::ColorConvertFloat4ToU32(c));
			ImGui::Text("Vertex count: %zu", selobj->mesh->getNumVertices());
			ImGui::Text("Quad count:   %zu", selobj->mesh->getNumQuads());
			ImGui::Text("Tri count:    %zu", selobj->mesh->getNumTris());
			ImGui::Text("Weird: 0x%X", selobj->mesh->weird);
			if (selobj->mesh->extension) {
				ImGui::TextUnformatted("--- EXTENSION ---");
				ImGui::Text("Unk: %u", selobj->mesh->extension->extUnk2);
				ImGui::Text("Frames size: %zu", selobj->mesh->extension->frames.size());
				ImGui::Text("Name: %s", selobj->mesh->extension->name.c_str());
			}
		}
		if (selobj->line && ImGui::CollapsingHeader("Line")) {
			ImVec4 c = ImGui::ColorConvertU32ToFloat4(swap_rb(selobj->color));
			if (ImGui::ColorEdit4("Color", &c.x, 0))
				selobj->color = swap_rb(ImGui::ColorConvertFloat4ToU32(c));
			ImGui::Text("Vertex count: %zu", selobj->line->getNumVertices());
			std::string termstr;
			for (uint32_t t : selobj->line->terms)
				termstr += std::to_string(t) + ',';
			ImGui::Text("Terms: %s", termstr.c_str());

		}
		if (selobj->mesh && ImGui::CollapsingHeader("FTXO")) {
			// TODO: place this in "DebugUI.cpp"
			if (ImGui::Button("Change texture")) {
				uint16_t* ftxFace = (uint16_t*)selobj->mesh->ftxFaces.data();
				uint32_t numFaces = selobj->mesh->ftxFaces.size();
				for (size_t i = 0; i < numFaces; ++i) {
					ftxFace[2] = curtexid;
					ftxFace += 6;
				}
				InvalidateMesh(selobj->mesh.get());
			}
			ImGui::SameLine();
			static Mesh::FTXFace newFace{ 0,0,0,0,0,0 };
			if (ImGui::Button("Edit")) {
				ImGui::OpenPopup("EditFTX");
			}
			if (ImGui::BeginPopup("EditFTX")) {
				for(int i = 0; i < 6; ++i)
					ImGui::InputScalar(std::to_string(i).c_str(), ImGuiDataType_U16, &newFace[i], nullptr, nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
				if (ImGui::Button("Apply")) {
					for (auto& ftxFace : selobj->mesh->ftxFaces)
						ftxFace = newFace;
					InvalidateMesh(selobj->mesh.get());
				}
				ImGui::EndPopup();
			}
			if (!selobj->mesh->ftxFaces.empty()) {
				float* uvCoords = (float*)selobj->mesh->textureCoords.data();
				float* uvCoords2 = (float*)selobj->mesh->lightCoords.data();
				uint16_t* ftxFace = (uint16_t*)selobj->mesh->ftxFaces.data();
				size_t numFaces = selobj->mesh->getNumQuads() + selobj->mesh->getNumTris();
				size_t numTexFaces = 0, numLitFaces = 0;
				for (auto& ftxFace : selobj->mesh->ftxFaces) {
					if (ftxFace[0] & 0x20) ++numTexFaces;
					if (ftxFace[0] & 0x80) ++numLitFaces;
				}
				ImGui::Text("Num     Faces:  %zu (%zu)", selobj->mesh->ftxFaces.size(), numFaces);
				ImGui::Text("Num Tex Faces:  %zu (%zu)", selobj->mesh->textureCoords.size() / 8, numTexFaces);
				ImGui::Text("Num Lit Faces:  %zu (%zu)", selobj->mesh->lightCoords.size() / 8, numLitFaces);
				ImGui::Separator();
				for (size_t i = 0; i < numFaces; ++i) {
					ImGui::Text("%04X %04X %04X %04X %04X %04X", ftxFace[0], ftxFace[1], ftxFace[2], ftxFace[3], ftxFace[4], ftxFace[5]);
					if (ftxFace[0] & 0x20) {
						ImGui::Text(" t (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f)", uvCoords[0], uvCoords[1], uvCoords[2], uvCoords[3], uvCoords[4], uvCoords[5], uvCoords[6], uvCoords[7]);
						uvCoords += 8;
					}
					if (ftxFace[0] & 0x80) {
						ImGui::Text(" l (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f)", uvCoords2[0], uvCoords2[1], uvCoords2[2], uvCoords2[3], uvCoords2[4], uvCoords2[5], uvCoords2[6], uvCoords2[7]);
						uvCoords2 += 8;
					}
					ftxFace += 6;
				}
			}
			else
				ImGui::Text("No FTX");
		}
		if (selobj->light && ImGui::CollapsingHeader("Light"))
		{
			char s[] = "Param ?\0";
			for (int i = 0; i < 7; i++)
			{
				s[6] = '0' + i;
				ImGui::InputScalar(s, ImGuiDataType_U32, &selobj->light->param[i], 0, 0, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
			}
		}
		if (selobj->excChunk && ImGui::CollapsingHeader("EXC")) {
			if (ImGui::Button("Export EXC")) {
				auto fpath = GuiUtils::SaveDialogBox("Bin file\0*.*\0\0\0\0\0", "bin");
				if (!fpath.empty()) {
					FILE* file;
					_wfopen_s(&file, fpath.c_str(), L"wb");
					if (file) {
						auto str = selobj->excChunk->saveToString();
						fwrite(str.data(), str.size(), 1, file);
						fclose(file);
					}
				}
			}
			if (ImGui::Button("Dump HMTX")) {
				Chunk* hmtx = selobj->excChunk->findSubchunk('HMTX');
				int mid = 0;
				for (auto& md : hmtx->multidata) {
					double* mtx = (double*)md.data();
					printf("--- Matrix %i ---\n", mid++);
					for (int r = 0; r < 4; ++r) {
						printf("  %18.12f %18.12f %18.12f\n", mtx[0], mtx[1], mtx[2]);
						mtx += 3;
					}
				}
			}
			auto walkChunk = [](Chunk* chk, auto& rec) -> void {
				std::string tag{ (char*)&chk->tag, 4};
				if (ImGui::TreeNode(chk, "%s", tag.c_str())) {
					for (Chunk& sub : chk->subchunks)
						rec(&sub, rec);
					ImGui::TreePop();
				}
			};
			walkChunk(selobj->excChunk.get(), walkChunk);
		}
		if (wannadel)
		{
			if (selobj->getRefCount() > 0u)
				warn("It's not possible to remove an object that is referenced by other objects!");
			else {
				g_scene.RemoveObject(selobj);
				selobj = 0;
			}
		}
	}
	ImGui::End();
	if (nextobjtosel) selobj = nextobjtosel;
}

void CmdSaveScene()
{
	auto newfn = std::filesystem::path(g_scene.lastspkfn).filename().string();
	size_t atpos = newfn.rfind('@');
	if (atpos != newfn.npos)
		newfn = newfn.substr(atpos + 1);

	auto zipPath = GuiUtils::SaveDialogBox("Scene ZIP archive\0*.zip\0\0\0", "zip", newfn, "Save Scene ZIP archive as...");
	if (!zipPath.empty())
		g_scene.SaveSceneSPK(zipPath.string().c_str());
}

void IGMain()
{
	ImGui::SetNextWindowPos(ImVec2(965, 453), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(310, 203), ImGuiCond_FirstUseEver);
	ImGui::Begin("c47edit");
	if (ImGui::Button("Save Scene")) {
		CmdSaveScene();
	}
	ImGui::SameLine();
	ImGui::Text("%4u FPS", framespersec);
	ImGui::DragFloat("Cam speed", &camspeed, 0.1f);
	ImGui::DragFloat3("Cam pos", &campos.x, 1.0f);
	ImGui::DragFloat2("Cam ori", &camori.x, 0.1f);
	ImGui::DragFloat2("Cam dist", &camNearDist, 1.0f);
	ImGui::DragFloat3("Cursor pos", &cursorpos.x);
	ImGui::Checkbox("Wireframe", &wireframe);
	ImGui::SameLine();
	ImGui::Checkbox("Textured", &rendertextures);
	ImGui::SameLine();
	ImGui::Checkbox("EXC", &renderExc);
	ImGui::Checkbox("Diffuse Tex", &renderColorTextures);
	ImGui::SameLine();
	ImGui::Checkbox("Lightmaps", &renderLightmaps);
	ImGui::SameLine();
	ImGui::Checkbox("Alpha Test", &enableAlphaTest);
	ImGui::Checkbox("Gates", &showZGates);
	ImGui::SameLine();
	ImGui::Checkbox("Bounds", &showZBounds);
	ImGui::SameLine();
	ImGui::Checkbox("Invisible Objects", &showInvisibleObjects);
	ImGui::Checkbox("Untextured Faces in Tex mode", &renderUntexturedFaces);
	ImGui::End();
}

void IGTextures()
{
	ImGui::SetNextWindowSize(ImVec2(512.0f, 350.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Textures", &wndShowTextures);

	if (ImGui::Button("Add")) {
		auto filepaths = GuiUtils::MultiOpenDialogBox("Image\0*.png;*.bmp;*.jpg;*.jpeg;*.gif\0\0\0\0", "png");
		for (const auto& filepath : filepaths) {
			AddTexture(g_scene, filepath);
			GlifyTexture(&g_scene.palPack.subchunks.back());
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Replace")) {
		auto [palchk, dxtchk] = FindTextureChunk(g_scene, curtexid);
		if (palchk) {
			auto fpath = GuiUtils::OpenDialogBox("Image\0*.png;*.bmp;*.jpg;*.jpeg;*.gif\0\0\0\0", "png");
			if (!fpath.empty()) {
				uint32_t tid = *(uint32_t*)palchk->maindata.data();
				ImportTexture(fpath, *palchk, *dxtchk, tid);
				InvalidateTexture(tid);
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Export")) {
		Chunk* palchk = FindTextureChunk(g_scene, curtexid).first;
		if (palchk) {
			TexInfo* ti = (TexInfo*)palchk->maindata.data();
			auto fpath = GuiUtils::SaveDialogBox("PNG Image\0*.png\0\0\0\0", "png", ti->getName());
			if (!fpath.empty()) {
				ExportTexture(palchk, fpath);
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Export all")) {
		auto dirpath = GuiUtils::SelectFolderDialogBox("Export all the textures in PNG to:");
		if (!dirpath.empty()) {
			for (Chunk& chk : g_scene.palPack.subchunks) {
				TexInfo* ti = (TexInfo*)chk.maindata.data();
				std::string name = ti->name;
				if (name.empty()) {
					char tbuf[20];
					sprintf_s(tbuf, "NoName%04X", ti->id);
					name = tbuf;
				}
				ExportTexture(&chk, dirpath / (name + ".png"));
			}
		}
	}

	static int packShown = 0;
	ImGui::SameLine();
	ImGui::RadioButton("Tex", &packShown, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Lgt", &packShown, 1);
	auto& pack = (packShown == 0) ? g_scene.palPack : g_scene.lgtPack;

	// Texture size conformance
	// Verifies if the texture size is allowed by some or all renderers provided by the game.
	static const ImVec4 conformanceColor[3] = { ImVec4(1.0f,1.0f,1.0f,1.0f), ImVec4(1.0f,1.0f,0.0f,1.0f), ImVec4(1.0f,0.0f,0.0f,1.0f) };
	static const char* const conformanceText[3] = {
		"Texture size looks fine!\nShould work on all renderers and drivers.",
		"width or height is not a power of 2!\nWill work on Direct3D+OpenGL but not Glide.",
		"width or height is not a multiple of 4!\nWill not work on Direct3D with texture compression, nor Glide."
	};
	auto getConformanceLevel = [](int width, int height) {
		// not multiple of 4 -> red
		if ((width % 4) != 0 || (height % 4) != 0)
			return 2;
		// not power of two -> yellow
		if ((width & (width - 1)) || (height & (height - 1)))
			return 1;
		// otherwise -> green/white
		return 0;
	};

	if (ImGui::BeginTable("TextureColumnsa", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoHostExtendY | ImGuiTableFlags_NoHostExtendX, ImGui::GetContentRegionAvail())) {
		ImGui::TableSetupColumn("TexListCol", ImGuiTableColumnFlags_WidthFixed, 256.0f);
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::BeginChild("TextureList");
		for (Chunk& chk : pack.subchunks) {
			TexInfo* ti = (TexInfo*)chk.maindata.data();
			ImGui::PushID(ti);
			static const float imgsize = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
			if (ImGui::Selectable("##texture", curtexid == ti->id, 0, ImVec2(0.0f, imgsize)))
				curtexid = ti->id;
			if (ImGui::IsItemVisible()) {
				ImGui::SameLine();
				auto t = texmap.find(ti->id);
				ImGui::Image((t != texmap.end()) ? t->second : nullptr, ImVec2(imgsize, imgsize));
				ImGui::SameLine();
				ImGui::BeginGroup();
				ImGui::Text("%i: %s", ti->id, ti->name);
				ImGui::TextColored(conformanceColor[getConformanceLevel(ti->width, ti->height)], "%i*%i", ti->width, ti->height);
				ImGui::EndGroup();
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::TableNextColumn();
		if (Chunk* palchk = FindTextureChunk(g_scene, curtexid).first) {
			TexInfo* ti = (TexInfo*)palchk->maindata.data();
			ImGui::Text("ID: %i\nSize: %i*%i\nNum mipmaps: %i\nFlags: %08X\nUnknown: %08X\nName: %s", ti->id, ti->width, ti->height, ti->numMipmaps, ti->flags, ti->random, ti->name);
			auto conform = getConformanceLevel(ti->width, ti->height);
			ImGui::TextColored(conformanceColor[conform], "%s", conformanceText[conform]);
			ImGui::Image(texmap.at(curtexid), ImVec2(ti->width, ti->height));
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

void IGSounds()
{
	static int selectedSoundId = -1;

	auto getWaveDataIndex = [](WaveAudioObject* wave) {
		int index = 0;
		for (auto& ptr : g_scene.audioMgr.audioObjects) {
			if (ptr && ptr->getType() == WaveAudioObject::TYPEID) {
				if (ptr.get() == wave)
					return index;
				index += 1;
			}
		}
		return -1;
	};
	WaveAudioObject* selectedWave = g_scene.audioMgr.getObjectAs<WaveAudioObject>(selectedSoundId);
	int selectedWaveIndex = getWaveDataIndex(selectedWave);

	ImGui::SetNextWindowSize(ImVec2(512.0f, 350.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Waves", &wndShowSounds);
	ImGui::BeginDisabled(!selectedWave);
	if (ImGui::Button("Replace")) {
		auto fpath = GuiUtils::OpenDialogBox("Sound Wave file (*.wav)\0*.WAV\0\0\0", "wav");
		if (!fpath.empty()) {
			Chunk& chk = g_scene.wavPack.subchunks[selectedWaveIndex];
			FILE* file;
			_wfopen_s(&file, fpath.c_str(), L"rb");
			if (file) {
				fseek(file, 0, SEEK_END);
				size_t len = ftell(file);
				fseek(file, 0, SEEK_SET);
				chk.maindata.resize(len);
				fread(chk.maindata.data(), len, 1, file);
				fclose(file);
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Export")) {
		const char* sndName = g_scene.audioMgr.audioNames[selectedSoundId].c_str();
		auto fpath = GuiUtils::SaveDialogBox("Sound Wave file (*.wav)\0*.WAV\0\0\0", "wav", std::filesystem::path(sndName).filename());
		if (!fpath.empty()) {
			Chunk& chk = g_scene.wavPack.subchunks[selectedWaveIndex];
			FILE* file;
			_wfopen_s(&file, fpath.c_str(), L"wb");
			if (file) {
				fwrite(chk.maindata.data(), chk.maindata.size(), 1, file);
				fclose(file);
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Export all")) {
		auto dirpath = GuiUtils::SelectFolderDialogBox("Export all WAV sounds to:\n(this will also create subfolders)");
		if (!dirpath.empty()) {
			for (size_t id = 1; id < g_scene.audioMgr.audioObjects.size(); ++id) {
				auto& ptr = g_scene.audioMgr.audioObjects[id];
				auto& name = g_scene.audioMgr.audioNames[id];
				if (ptr && ptr->getType() == WaveAudioObject::TYPEID) {
					WaveAudioObject* wave = (WaveAudioObject*)ptr.get();
					Chunk& chk = g_scene.wavPack.subchunks[getWaveDataIndex(wave)];
					auto fpath = dirpath / std::filesystem::path(name).relative_path();
					std::filesystem::create_directories(fpath.parent_path());
					FILE* file;
					_wfopen_s(&file, fpath.c_str(), L"wb");
					if (file) {
						fwrite(chk.maindata.data(), chk.maindata.size(), 1, file);
						fclose(file);
					}
				}
			}
		}
	}
	ImGui::BeginChild("SoundList");
	for (size_t id = 1; id < g_scene.audioMgr.audioObjects.size(); ++id) {
		auto& ptr = g_scene.audioMgr.audioObjects[id];
		auto& name = g_scene.audioMgr.audioNames[id];
		if (ptr && ptr->getType() == WaveAudioObject::TYPEID) {
			WaveAudioObject* wave = (WaveAudioObject*)ptr.get();
			Chunk& chk = g_scene.wavPack.subchunks[getWaveDataIndex(wave)];
			ImGui::PushID(id);
			if (ImGui::Selectable("##Sound", selectedSoundId == id)) {
				selectedSoundId = id;
				PlaySoundA(nullptr, nullptr, 0);
				// copy for playing, to prevent sound corruption when sound is replaced/deleted while being played
				static Chunk::DataBuffer playingWav;
				playingWav = chk.maindata;
				PlaySoundA((const char*)playingWav.data(), nullptr, SND_MEMORY | SND_ASYNC);
			}
			ImGui::SameLine();
			ImGui::Text("%3i: %s", id, name.c_str());
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

void IGAudioObjects()
{
	static uint32_t selectedAudioObjId = 0;

	ImGui::SetNextWindowSize(ImVec2(512.0f, 350.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Audio objects", &wndShowAudioObjects);
	if (ImGui::BeginTable("AudioObjTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoHostExtendY | ImGuiTableFlags_NoHostExtendX, ImGui::GetContentRegionAvail())) {
		ImGui::TableSetupColumn("AudioListCol", ImGuiTableColumnFlags_WidthFixed, 256.0f);
		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		auto listType = [](int typeId, const char* typeName) {
			if (ImGui::CollapsingHeader(typeName)) {
				for (uint32_t id = 1; id < g_scene.audioMgr.audioObjects.size(); ++id) {
					auto& ptr = g_scene.audioMgr.audioObjects[id];
					auto& name = g_scene.audioMgr.audioNames[id];
					if (ptr && ptr->getType() == typeId) {
						ImGui::PushID(id);
						if (ImGui::Selectable("##AudioObject", selectedAudioObjId == id)) {
							selectedAudioObjId = id;
						}
						if (ImGui::BeginDragDropSource()) {
							ImGui::SetDragDropPayload("AudioRef", &id, 4);
							std::string preview = std::to_string(id) + ": " + name;
							ImGui::TextUnformatted(preview.c_str());
							ImGui::EndDragDropSource();
						}
						ImGui::SameLine();
						ImGui::Text("%4u: %s", id, name.c_str());
						ImGui::PopID();
					}
				}
			}
		};
		ImGui::BeginChild("AudioListWnd");
		listType(SoundAudioObject::TYPEID, "Sounds");
		listType(SetAudioObject::TYPEID, "Sets");
		listType(MaterialAudioObject::TYPEID, "Materials");
		listType(ImpactAudioObject::TYPEID, "Impacts");
		listType(RoomAudioObject::TYPEID, "Rooms");
		ImGui::EndChild();

		ImGui::TableNextColumn();

		AudioObject* obj = g_scene.audioMgr.getObject(selectedAudioObjId);
		struct ImGuiListener {
			void member(uint32_t& val, const char* name) { ImGui::InputScalar(name, ImGuiDataType_S32, &val); }
			void member(float& val, const char* name) { ImGui::InputFloat(name, &val); }
			void member(std::string& val, const char* name) { IGStdStringInput(name, val); }
			void member(AudioRef& val, const char* name) { IGAudioRef(name, val); }
		};
		ImGuiListener iglisten;
		if (obj) {
			if (obj->getType() == SoundAudioObject::TYPEID) ((SoundAudioObject*)obj)->reflect(iglisten);
			else if (obj->getType() == MaterialAudioObject::TYPEID) ((MaterialAudioObject*)obj)->reflect(iglisten);
			else if (obj->getType() == ImpactAudioObject::TYPEID) ((ImpactAudioObject*)obj)->reflect(iglisten);
			else if (obj->getType() == RoomAudioObject::TYPEID) ((RoomAudioObject*)obj)->reflect(iglisten);
			else if (obj->getType() == SetAudioObject::TYPEID) {
				SetAudioObject* set = (SetAudioObject*)obj;
				set->reflect(iglisten);
				ImGui::Separator();
				uint32_t numSounds = (uint32_t)set->sounds.size();
				if (ImGui::InputScalar("numSounds", ImGuiDataType_U32, &numSounds))
					set->sounds.resize(numSounds);
				int index = 0;
				for (auto& entry : set->sounds)
					IGAudioRef(std::to_string(index++).c_str(), entry);
			}
			else ImGui::TextUnformatted(":(");
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

void IGZDefines()
{
	auto classMembers = ClassInfo::ProcessClassMemberListString(g_scene.zdefTypes);
	std::vector<ClassInfo::ObjectMember> members;
	ClassInfo::AddDBLMemberInfo(members, classMembers);

	nextobjtosel = nullptr;
	ImGui::Begin("ZDefines", &wndShowZDefines);
	IGDBLList(g_scene.zdefValues, members);
	ImGui::End();
	if (nextobjtosel)
		selobj = nextobjtosel;
}

GameObject* FindObjectNamed(const char *name, GameObject *sup = g_scene.rootobj)
{
	if (sup->name == name)
		return sup;
	else
		for (auto e = sup->subobj.begin(); e != sup->subobj.end(); e++) {
			GameObject *r = FindObjectNamed(name, *e);
			if (r) return r;
		}
	return 0;
}

void RenderObject(GameObject *o, const Matrix& parentTransform)
{
	Matrix transform = o->matrix * parentTransform;
	if (o->mesh && (o->flags & 0x20) && IsObjectVisible(o)) {
		if (!rendertextures) {
			uint32_t clr = swap_rb(o->color);
			glColor4ubv((uint8_t*)&clr);
		}
		DrawMesh(o->mesh.get(), transform, o->excChunk.get());
	}
	for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
		RenderObject(*e, transform);
}

Vector3 finalintersectpnt = Vector3(0, 0, 0);

template <int numverts>
bool IsRayIntersectingFace(const Vector3& raystart, const Vector3& raydir, float* bver, uint16_t* bfac, const Matrix& worldmtx)
{
	Vector3 pnts[numverts];
	for (int i = 0; i < 3; i++)
	{
		Vector3 v(bver[bfac[i] * 3 / 2], bver[bfac[i] * 3 / 2 + 1], bver[bfac[i] * 3 / 2 + 2]);
		pnts[i] = v.transform(worldmtx);
	}

	Vector3 edges[numverts];
	for (int i = 0; i < 2; i++)
		edges[i] = pnts[i + 1] - pnts[i];

	Vector3 planenorm = edges[1].cross(edges[0]);
	float planeord = -planenorm.dot(pnts[0]);

	float planenorm_dot_raydir = planenorm.dot(raydir);
	if (planenorm_dot_raydir >= 0) return false;

	float param = -(planenorm.dot(raystart) + planeord) / planenorm_dot_raydir;
	if (param < 0) return false;

	Vector3 interpnt = raystart + raydir * param;

	if constexpr (numverts >= 4) {
		for (int i = 3; i < numverts; i++)
		{
			Vector3 v(bver[bfac[i] * 3 / 2], bver[bfac[i] * 3 / 2 + 1], bver[bfac[i] * 3 / 2 + 2]);
			pnts[i] = v.transform(worldmtx);
		}

		for (int i = 2; i < numverts - 1; i++)
			edges[i] = pnts[i + 1] - pnts[i];
	}
	edges[numverts - 1] = pnts[0] - pnts[numverts - 1];

	// Check if plane/ray intersection point is inside face

	for (int i = 0; i < numverts; i++)
	{
		Vector3 edgenorm = -planenorm.cross(edges[i]);
		Vector3 ptoi = interpnt - pnts[i];
		if (edgenorm.dot(ptoi) < 0)
			return false;
	}

	finalintersectpnt = interpnt;
	return true;
}

GameObject *IsRayIntersectingObject(const Vector3& raystart, const Vector3& raydir, GameObject *o, const Matrix& worldmtx)
{
	float d;
	Matrix objmtx = o->matrix * worldmtx;
	if (o->mesh && IsObjectVisible(o))
	{
		Mesh *m = o->mesh.get();
		float* vertices = (o->excChunk && o->excChunk->findSubchunk('LCHE')) ? ApplySkinToMesh(m, o->excChunk.get()) : m->vertices.data();
		for (size_t i = 0; i < m->getNumQuads(); i++)
			if (IsRayIntersectingFace<4>(raystart, raydir, vertices, m->quadindices.data() + i * 4, objmtx))
				if ((d = (finalintersectpnt - campos).sqlen2xz()) < bestpickdist)
				{
					bestpickdist = d;
					bestpickobj = o;
					bestpickintersectionpnt = finalintersectpnt;
				}
		for(size_t i = 0; i < m->getNumTris(); i++)
			if (IsRayIntersectingFace<3>(raystart, raydir, vertices, m->triindices.data() + i * 3, objmtx))
				if ((d = (finalintersectpnt - campos).sqlen2xz()) < bestpickdist)
				{
					bestpickdist = d;
					bestpickobj = o;
					bestpickintersectionpnt = finalintersectpnt;
				}
	}
	for (auto c = o->subobj.begin(); c != o->subobj.end(); c++)
		IsRayIntersectingObject(raystart, raydir, *c, objmtx);
	return 0;
}

void UIClean()
{
	UncacheAllTextures();
	UncacheAllMeshes();
	selobj = nullptr;
	objVisibilityMap.clear();
	bestpickobj = nullptr;
	objtogive = nullptr;
	nextobjtosel = nullptr;
}

bool CmdOpenScene()
{
	auto zipPath = GuiUtils::OpenDialogBox("Scene ZIP archive\0*.zip\0\0\0", "zip", "Select a Scene ZIP archive (containing Pack.SPK)");
	if (zipPath.empty())
		return false;
	UIClean();
	g_scene.LoadSceneSPK(zipPath.string().c_str());
	GlifyAllTextures();
	return true;
}

void CmdNewScene()
{
	if (MessageBoxW(hWindow, L"Create a new empty scene?", L"c47edit", MB_ICONWARNING | MB_YESNO) == IDYES) {
		UIClean();
		g_scene.LoadEmpty();
	}
}

#ifndef APPVEYOR
int main(int argc, char* argv[])
#else
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, char *args, int winmode)
#endif
{
	//SetProcessDPIAware();

	try {
		ClassInfo::ReadClassInfo();
	}
	catch (const std::exception& ex) {
		std::string err = "Failed to read the file classes.json.\n\nBe sure the file classes.json is present in the same folder that contains the c47edit executable.\n\nReason:\n";
		err += ex.what();
		MessageBoxA(nullptr, err.c_str(), "c47edit", 16);
		exit(-2);
	}

	bool appnoquit = true;
	InitWindow();

	ImGui::CreateContext(0);
	ImGui::GetStyle().WindowRounding = 7.0f;
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui_ImplWin32_Init((void*)hWindow);
	ImGui_ImplOpenGL2_Init();
	lastfpscheck = GetTickCount();

	if (!CmdOpenScene())
		g_scene.LoadEmpty();

	while (appnoquit = HandleWindow())
	{
		if (win_minimized)
			Sleep(100);
		else
		{
			Vector3 cd(0, 0, 1), ncd;
			Matrix m1 = Matrix::getRotationXMatrix(camori.x);
			Matrix m2 = Matrix::getRotationYMatrix(camori.y);
			Matrix crm = m1 * m2; // order?
			//CreateRotationYXZMatrix(&crm, camori.y, camori.x, 0);
			ncd = cd.transform(crm);
			Vector3 crabnn;
			crabnn = Vector3(0, 1, 0).cross(ncd);
			Vector3 crab = crabnn.normal();

			Vector3 cammove(0, 0, 0);
			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantCaptureKeyboard)
			{
				if (ImGui::IsKeyDown((ImGuiKey)VK_LEFT) || ImGui::IsKeyDown((ImGuiKey)'A') || ImGui::IsKeyDown((ImGuiKey)'Q'))
					cammove -= crab;
				if (ImGui::IsKeyDown((ImGuiKey)VK_RIGHT) || ImGui::IsKeyDown((ImGuiKey)'D'))
					cammove += crab;
				if (ImGui::IsKeyDown((ImGuiKey)VK_UP) || ImGui::IsKeyDown((ImGuiKey)'W') || ImGui::IsKeyDown((ImGuiKey)'Z'))
					cammove += ncd;
				if (ImGui::IsKeyDown((ImGuiKey)VK_DOWN) || ImGui::IsKeyDown((ImGuiKey)'S'))
					cammove -= ncd;
				if (ImGui::IsKeyDown((ImGuiKey)'R'))
					cammove.y += 1;
				if (ImGui::IsKeyDown((ImGuiKey)'F'))
					cammove.y -= 1;
				if (ImGui::IsKeyPressed((ImGuiKey)'Y'))
					wireframe = !wireframe;
				if (ImGui::IsKeyPressed((ImGuiKey)'T'))
					rendertextures = !rendertextures;
			}
			campos += cammove * camspeed * (io.KeyShift ? 2.0f : 1.0f);
			if (io.MouseDown[0] && !io.WantCaptureMouse && !(io.KeyAlt || io.KeyCtrl))
			{
				camori.y += io.MouseDelta.x * 0.01f;
				camori.x += io.MouseDelta.y * 0.01f;
			}
			if (!io.WantCaptureMouse)
			if (io.MouseClicked[1] || (io.MouseClicked[0] && (io.KeyAlt || io.KeyCtrl)))
			{
				Vector3 raystart, raydir;
				float ys = 1.0f / tan(60.0f * (float)M_PI / 180.0f / 2.0f);
				float xs = ys / ((float)screen_width / (float)screen_height);
				ImVec2 mspos = ImGui::GetMousePos();
				float msx = mspos.x * 2.0f / (float)screen_width - 1.0f;
				float msy = mspos.y * 2.0f / (float)screen_height - 1.0f;
				Vector3 hi = ncd.cross(crab);
				raystart = campos + ncd + crab * (msx / xs) - hi * (msy / ys);
				raydir = raystart - campos;

				bestpickobj = 0;
				bestpickdist = std::numeric_limits<float>::infinity();
				IsRayIntersectingObject(raystart, raydir, g_scene.superroot, Matrix::getIdentity());
				if (io.KeyAlt) {
					if (bestpickobj && selobj)
						selobj->matrix.setTranslationVector(bestpickintersectionpnt);
				}
				else {
					selobj = bestpickobj;
					if (io.MouseDoubleClicked[0] || io.MouseDoubleClicked[1])
						selobj = selobj->parent;
				}
				cursorpos = bestpickintersectionpnt;
			}

			Matrix persp = Matrix::getLHPerspectiveMatrix(60.0f * (float)M_PI / 180.0f, (float)screen_width / (float)screen_height, camNearDist, camFarDist);
			Matrix lookat = Matrix::getLHLookAtViewMatrix(campos, campos + ncd, Vector3(0.0f, 1.0f, 0.0f));

			ImGui_ImplOpenGL2_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

			if (selobj) {
				ImGuizmo::BeginFrame();
				ImGuizmo::SetRect(0.0f, 0.0f, (float)screen_width, (float)screen_height);
				Matrix parentMat = Matrix::getIdentity();
				for (GameObject* par = selobj->parent; par; par = par->parent)
					parentMat *= par->matrix;
				Matrix globalMat = selobj->matrix * parentMat;
				if (ImGuizmo::Manipulate(lookat.v, persp.v, ImGuizmo::TRANSLATE | ImGuizmo::ROTATE, ImGuizmo::WORLD, globalMat.v))
					selobj->matrix = globalMat * parentMat.getInverse4x3();
			}

			IGMain();
			IGObjectTree();
			IGObjectInfo();
#ifndef APPVEYOR
			IGDebugWindows();
#endif
			if (wndShowTextures) IGTextures();
			if (wndShowSounds) IGSounds();
			if (wndShowAudioObjects) IGAudioObjects();
			if (wndShowZDefines) IGZDefines();
			if (ImGui::BeginMainMenuBar()) {
				if (ImGui::BeginMenu("Scene")) {
					if (ImGui::MenuItem("New"))
						CmdNewScene();
					if (ImGui::MenuItem("Open..."))
						CmdOpenScene();
					if (ImGui::MenuItem("Save as..."))
						CmdSaveScene();
					ImGui::Separator();
					if (ImGui::MenuItem("Exit"))
						DestroyWindow(hWindow);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Create")) {
					static const std::pair<uint16_t, const char*> categories[] = {
						{0x0010, "Group"},
						{0x0020, "Mesh"},
						{0x0040, "Camera"},
						{0x0080, "Light"},
						{0x0400, "Shape"},
						{0x0800, "List"},
					};
					for (auto& [flags, cat] : categories) {
						if (ImGui::BeginMenu(cat)) {
							for (auto& [name, id] : g_classInfo_stringIdMap) {
								if (ClassInfo::GetObjTypeCategory(id) & flags) {
									if (ImGui::MenuItem(name.c_str())) {
										g_scene.CreateObject(id, g_scene.rootobj);
									}
								}
							}
							ImGui::EndMenu();
						}
					}
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Tools")) {
					ImGui::MenuItem("Textures", nullptr, &wndShowTextures);
					ImGui::MenuItem("Waves", nullptr, &wndShowSounds);
					ImGui::MenuItem("Audio objects", nullptr, &wndShowAudioObjects);
					ImGui::MenuItem("ZDefines", nullptr, &wndShowZDefines);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("Help")) {
					if (ImGui::MenuItem("c47edit Wiki")) {
						ShellExecuteA(hWindow, nullptr, "https://github.com/AdrienTD/c47edit/wiki", nullptr, nullptr, SW_SHOWNORMAL);
					}
					if (ImGui::MenuItem("About...")) {
						MessageBox(hWindow, "c47edit\nUnofficial scene editor for \"Hitman: Codename 47\"\n\n"
							"(C) 2018-2023 AdrienTD\nLicensed under the GPL 3.\nSee LICENSE file for details.\n"
							"See https://github.com/AdrienTD/c47edit#libraries-used for copyright and licensing of 3rd-party libraries.", "c47edit", 0);
					}
					ImGui::EndMenu();
				}
#ifndef APPVEYOR
				IGDebugMenus();
#endif
				float barwidth = ImGui::GetWindowWidth() - ImGui::GetStyle().ItemSpacing.x * 2.0f;
				ImGui::SameLine(barwidth - ImGui::CalcTextSize(APP_VERSION).x);
				ImGui::TextUnformatted(APP_VERSION);
				ImGui::EndMainMenuBar();
			}

			// First time message
			if (objVisibilityMap.empty()) {
				ImGui::SetNextWindowPos(ImVec2((float)screen_width * 0.5f, (float)screen_height * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
				ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
				ImGui::Begin("FirstTimeMessage", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
				ImGui::TextUnformatted("Welcome!\nNo object is rendered at the moment.\nTo render the scene, look at the Scene graph window,\nthen hold SHIFT and click a ROOT object.");
				ImGui::End();
			}

			ImGui::EndFrame();

			BeginDrawing();
			glClearColor(0.5f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			glClearDepth(1.0f);
			glMatrixMode(GL_PROJECTION);
			Matrix projMatrix = lookat * persp;
			glLoadMatrixf(projMatrix.v);
			glMatrixMode(GL_MODELVIEW);

			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
			BeginMeshDraw();
			RenderObject(g_scene.superroot, Matrix::getIdentity());
			RenderMeshLists();
			EndMeshDraw();

			glLoadIdentity();
			if (renderExc) {
				glPointSize(5.0f);
				glBegin(GL_POINTS);
				auto renderAnim = [](auto rec, GameObject* obj, const Matrix& prevmat) -> void {
					Matrix mat = obj->matrix * prevmat;
					if (IsObjectVisible(obj)) {
						if (obj->excChunk) {
							Chunk& exchk = *obj->excChunk;
							assert(exchk.tag == 'HEAD');
							if (auto* keys = exchk.findSubchunk('KEYS')) {
								uint32_t cnt = *(uint32_t*)(keys->multidata[0].data());
								for (uint32_t i = 1; i <= cnt; ++i) {
									float* kpos = (float*)((char*)(keys->multidata[i].data()) + 4);
									Vector3 vpos = Vector3(kpos[0], kpos[1], kpos[2]).transform(mat);
									glVertex3fv(&vpos.x);
								}
							}
						}
					}
					for (auto& child : obj->subobj) {
						rec(rec, child, mat);
					}
				};
				renderAnim(renderAnim, g_scene.superroot, Matrix::getIdentity());
				glEnd();
				glPointSize(1.0f);
			}

			glDisable(GL_TEXTURE_2D);
			glPointSize(10);
			glColor3f(1, 1, 1);
			glBegin(GL_POINTS);
			glVertex3f(cursorpos.x, cursorpos.y, cursorpos.z);
			glEnd();
			glPointSize(1);

			ImGui::Render();
			ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
			EndDrawing();
			//_sleep(16);
			
			framesincursec++;
			uint32_t newtime = GetTickCount();
			if ((uint32_t)(newtime - lastfpscheck) >= 1000) {
				framespersec = framesincursec;
				framesincursec = 0;
				lastfpscheck = newtime;
			}
		}
	}
}
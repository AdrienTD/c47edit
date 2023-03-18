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

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <commdlg.h>
#include <mmsystem.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl2.h"
#include "imgui/imgui_impl_win32.h"

#include <stb_image.h>
#include <stb_image_write.h>

GameObject *selobj = 0, *viewobj = 0;
float objviewscale = 0.0f;
Vector3 campos(0, 0, -50), camori(0,0,0);
float camNearDist = 1.0f, camFarDist = 10000.0f;
float camspeed = 32;
bool wireframe = false;
bool findsel = false;
uint32_t framesincursec = 0, framespersec = 0, lastfpscheck;
Vector3 cursorpos(0, 0, 0);
bool renderExc = false;

GameObject *bestpickobj = 0;
float bestpickdist;
Vector3 bestpickintersectionpnt(0, 0, 0);

bool wndShowTextures = false;
bool wndShowSounds = false;

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

void IGOTNode(GameObject *o)
{
	bool op, colorpushed = 0;
	if (o == g_scene.superroot)
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (findsel)
		if (ObjInObj(selobj, o))
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	if (o == viewobj) {
		colorpushed = 1;
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 1, 0, 1));
	}
	op = ImGui::TreeNodeEx(o, (o->subobj.empty() ? ImGuiTreeNodeFlags_Leaf : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ((o == selobj) ? ImGuiTreeNodeFlags_Selected : 0), "%s(0x%X)::%s", ClassInfo::GetObjTypeString(o->type), o->type, o->name.c_str());
	if (colorpushed)
		ImGui::PopStyleColor();
	if (findsel)
		if (selobj == o)
			ImGui::SetScrollHereY();
	if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
		ImGuiIO& io = ImGui::GetIO();
		if (io.KeyShift)
			viewobj = o;
		else
		{
			selobj = o;
			cursorpos = selobj->position;
		}
	}
	if (ImGui::IsItemActive())
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload("GameObject", &o, sizeof(GameObject*));
			ImGui::Text("GameObject: %s", o->name.c_str());
			ImGui::EndDragDropSource();
		}
	if(op)
	{
		for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
			IGOTNode(*e);
		ImGui::TreePop();
	}
}

void IGObjectTree()
{
	ImGui::SetNextWindowPos(ImVec2(3, 3), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(316, 652), ImGuiCond_FirstUseEver);
	ImGui::Begin("Object tree", 0, ImGuiWindowFlags_HorizontalScrollbar);
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
		case 0xA:
		case 0xB:
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
	ImGui::SetNextWindowPos(ImVec2(1005, 3), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(270, 445), ImGuiCond_FirstUseEver);
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
		
		if (ImGui::Button("Find in tree"))
			findsel = true;
		if (selobj->parent)
		{
			ImGui::SameLine();
			if (ImGui::Button("Select parent"))
				selobj = selobj->parent;
		}
		if (ImGui::Button("Import OBJ")) {
			auto filepath = GuiUtils::OpenDialogBox("Wavefront OBJ model\0*.OBJ\0\0\0", "obj");
			if (!filepath.empty()) {
				std::vector<Vector3> objVertices;
				std::vector<Vector3> objTexCoords;
				std::vector<std::array<std::array<int, 3>, 3>> triangles;
				std::vector<std::array<std::array<int, 3>, 4>> quads;
				//std::vector<std::array<float, 8>> triUvs;
				//std::vector<std::array<float, 8>> quadUvs;

				ObjModel objmodel;
				objmodel.load(filepath);
				objVertices = std::move(objmodel.vertices);
				objTexCoords = std::move(objmodel.texCoords);
				for (auto& vec : objVertices)
					vec *= Vector3(-1.0f, 1.0f, 1.0f);
				//for (auto& tri : objmodel.triangles) {
				//	triIndices.insert(triIndices.end(), {tri[0], tri[1], tri[2]});
				//}
				triangles = std::move(objmodel.triangles);

				selobj->flags |= 0x20;
				if (!selobj->mesh)
					selobj->mesh = std::make_shared<Mesh>();
				else
					*selobj->mesh = {};

				Mesh* mesh = selobj->mesh.get();
				mesh->vertices.resize(3 * std::size(objVertices));
				mesh->quadindices.resize(4 * std::size(quads));
				mesh->triindices.resize(3 * std::size(triangles));
				size_t numFaces = std::size(quads) + std::size(triangles);
				mesh->ftxFaces.resize(numFaces);
				mesh->textureCoords.resize(numFaces * 8);
				mesh->lightCoords.resize(numFaces * 8);

				float* verts = mesh->vertices.data();
				uint16_t* qfaces = mesh->quadindices.data();
				uint16_t* tfaces = mesh->triindices.data();
				uint16_t* ftx = (uint16_t*)mesh->ftxFaces.data();
				float* uv1 = (float*)mesh->textureCoords.data();
				float* uv2 = (float*)mesh->lightCoords.data();

				memcpy(verts, objVertices.data(), objVertices.size() * 12);
				for (auto& tri : triangles)
					for (auto& [indPos, indTxc, indNrm] : tri)
						*tfaces++ = 2 * indPos;
				for (auto& quad : quads)
					for (auto& [indPos, indTxc, indNrm] : quad)
						*qfaces++ = 2 * indPos;
				size_t groupIndex = -1, triId = 0;
				static constexpr uint16_t defaultTexId = 0x0135;
				uint16_t texId = defaultTexId;
				for (auto& tri : triangles) {
					// find texture
					while (groupIndex == -1 || triId >= objmodel.groups[groupIndex].end) {
						groupIndex += 1;
						texId = defaultTexId;
						auto mit = objmodel.materials.find(objmodel.groups[groupIndex].name);
						if (mit != objmodel.materials.end()) {
							const std::string& texname = mit->second.map_Kd;
							for (auto& texchk : g_scene.palPack.subchunks) {
								const TexInfo* ti = (const TexInfo*)texchk.maindata.data();
								if (ti->name == texname) {
									texId = (uint16_t)ti->id;
								}
							}
						}
					}

					*ftx++ = 0x00A0; // 0: no texture, 0x20 textured, 0x80 lighting/shading
					*ftx++ = 0;
					*ftx++ = texId; // 0x0135;
					*ftx++ = 0xFFFF;
					*ftx++ = 0;
					*ftx++ = 0x0CF8;
					const Vector3& txc0 = objTexCoords[tri[0][1]];
					const Vector3& txc1 = objTexCoords[tri[1][1]];
					const Vector3& txc2 = objTexCoords[tri[2][1]];
					Vector3 txc3 = Vector3{ 0,0,0 };
					for (const Vector3& txc : { txc0, txc1, txc2, txc3 }) {
						*uv1++ = txc.x; *uv1++ = 1.0f - txc.y;
						*uv2++ = txc.x; *uv2++ = 1.0f - txc.y;
					}
					triId += 1;
				}

				InvalidateMesh(mesh);
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
		ImGui::Separator();

		IGStdStringInput("Name", selobj->name);
		ImGui::InputScalar("State", ImGuiDataType_U32, &selobj->state);
		ImGui::InputScalar("Type", ImGuiDataType_U32, &selobj->type);
		ImGui::InputScalar("Flags", ImGuiDataType_U32, &selobj->flags, nullptr, nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
		ImGui::Separator();
		ImGui::DragFloat3("Position", &selobj->position.x);
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
		ImGui::Text("Num. references: %u", selobj->refcount);
		if (ImGui::CollapsingHeader("DBL"))
		{
			auto members = ClassInfo::GetMemberNames(selobj);
			IGDBLList(selobj->dbl, members);
		}
		if (selobj->mesh && ImGui::CollapsingHeader("Mesh"))
		{
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
			if (selobj->refcount > 0)
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

void IGMain()
{
	ImGui::SetNextWindowPos(ImVec2(1005, 453), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(270, 203), ImGuiCond_FirstUseEver);
	ImGui::Begin("c47edit");
	ImGui::Text("c47edit - Version " APP_VERSION);
	if (ImGui::Button("Save Scene"))
	{
		auto newfn = std::filesystem::path(g_scene.lastspkfn).filename().string();
		size_t atpos = newfn.rfind('@');
		if (atpos != newfn.npos)
			newfn = newfn.substr(atpos + 1);

		auto zipPath = GuiUtils::SaveDialogBox("Scene ZIP archive\0*.zip\0\0\0", "zip", newfn, "Save Scene ZIP archive as...");
		if (!zipPath.empty())
			g_scene.SaveSceneSPK(zipPath.string().c_str());
	}
	ImGui::SameLine();
	if(ImGui::Button("About..."))
		MessageBox(hWindow, "c47edit\nUnofficial scene editor for \"Hitman: Codename 47\"\n\n"
			"(C) 2018 AdrienTD\nLicensed under the GPL 3.\nSee LICENSE file for details.\n\n"
			"3rd party libraries used:\n- Dear ImGui (MIT license)\n- Miniz (MIT license)\nSee LICENSE_* files for copyright and licensing of these libraries.", "c47edit", 0);
	//ImGui::DragFloat("Scale", &objviewscale, 0.1f);
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
	ImGui::Text("FPS: %u", framespersec);
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
			auto fpath = GuiUtils::SaveDialogBox("PNG Image\0*.png\0\0\0\0", "png", ti->name);
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
				ImGui::Text("%i: %s\n%i*%i", ti->id, ti->name, ti->width, ti->height);
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::TableNextColumn();
		if (Chunk* palchk = FindTextureChunk(g_scene, curtexid).first) {
			TexInfo* ti = (TexInfo*)palchk->maindata.data();
			ImGui::Text("ID: %i\nSize: %i*%i\nNum mipmaps: %i\nFlags: %08X\nUnknown: %08X\nName: %s", ti->id, ti->width, ti->height, ti->numMipmaps, ti->flags, ti->random, ti->name);
			ImGui::Image(texmap.at(curtexid), ImVec2(ti->width, ti->height));
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

void IGSounds()
{
	static int selectedSound = -1;
	Chunk* ands = g_scene.spkchk->findSubchunk('SDNA');
	if (!ands) return;
	Chunk* wavc = ands->findSubchunk('CVAW');
	if (!wavc) return;
	int32_t numSounds = *(uint32_t*)wavc->multidata[1].data();
	assert(wavc->multidata.size() == 2 + 2 * numSounds);
	assert((size_t)numSounds == g_scene.wavPack.subchunks.size());

	ImGui::SetNextWindowSize(ImVec2(512.0f, 350.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Sounds", &wndShowSounds);
	ImGui::BeginDisabled(!(selectedSound >= 0 && selectedSound < numSounds));
	if (ImGui::Button("Replace")) {
		auto fpath = GuiUtils::OpenDialogBox("Sound Wave file (*.wav)\0*.WAV\0\0\0", "wav");
		if (!fpath.empty()) {
			Chunk& chk = g_scene.wavPack.subchunks[selectedSound];
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
		const char* sndName = (const char*)wavc->multidata[2 + 2 * selectedSound + 1].data();
		auto fpath = GuiUtils::SaveDialogBox("Sound Wave file (*.wav)\0*.WAV\0\0\0", "wav", std::filesystem::path(sndName).filename());
		if (!fpath.empty()) {
			Chunk& chk = g_scene.wavPack.subchunks[selectedSound];
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
			for (int i = 0; i < numSounds; ++i) {
				Chunk& chk = g_scene.wavPack.subchunks[i];
				const char* sndName = (const char*)wavc->multidata[2 + 2 * i + 1].data();
				auto fpath = dirpath / std::filesystem::path(sndName).relative_path();
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
	ImGui::BeginChild("SoundList");
	for (int i = 0; i < numSounds; ++i) {
		Chunk& chk = g_scene.wavPack.subchunks[i];
		uint32_t sndId = *(uint32_t*)wavc->multidata[2 + 2 * i].data();
		const char* sndName = (const char*)wavc->multidata[2 + 2 * i + 1].data();
		ImGui::PushID(i);
		if (ImGui::Selectable("##Sound", selectedSound == i)) {
			selectedSound = i;
			PlaySoundA(nullptr, nullptr, 0);
			// copy for playing, to prevent sound corruption when sound is replaced/deleted while being played
			static Chunk::DataBuffer playingWav;
			playingWav = chk.maindata;
			PlaySoundA((const char*)playingWav.data(), nullptr, SND_MEMORY | SND_ASYNC);
		}
		ImGui::SameLine();
		ImGui::Text("%3i: %s", sndId, sndName);
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::End();
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

void RenderObject(GameObject *o)
{
	glPushMatrix();
	glTranslatef(o->position.x, o->position.y, o->position.z);
	glMultMatrixf(o->matrix.v);
	if (o->mesh && (o->flags & 0x20)) {
		if (!rendertextures) {
			uint32_t clr = swap_rb(o->color);
			glColor4ubv((uint8_t*)&clr);
		}
		DrawMesh(o->mesh.get(), o->excChunk.get());
	}
	for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
		RenderObject(*e);
	glPopMatrix();
}

Vector3 finalintersectpnt = Vector3(0, 0, 0);

bool IsRayIntersectingFace(Vector3 *raystart, Vector3 *raydir, float* bver, uint16_t* bfac, int numverts, Matrix *worldmtx)
{
	std::unique_ptr<Vector3[]> pnts = std::make_unique<Vector3[]>(numverts);
	for (int i = 0; i < 3; i++)
	{
		Vector3 v(bver[bfac[i] * 3 / 2], bver[bfac[i] * 3 / 2 + 1], bver[bfac[i] * 3 / 2 + 2]);
		pnts[i] = v.transform(*worldmtx);
	}

	std::unique_ptr<Vector3[]> edges = std::make_unique<Vector3[]>(numverts);
	for (int i = 0; i < 2; i++)
		edges[i] = pnts[i + 1] - pnts[i];

	Vector3 planenorm = edges[1].cross(edges[0]);
	float planeord = -planenorm.dot(pnts[0]);

	float planenorm_dot_raydir = planenorm.dot(*raydir);
	if (planenorm_dot_raydir >= 0) return false;

	float param = -(planenorm.dot(*raystart) + planeord) / planenorm_dot_raydir;
	if (param < 0) return false;

	Vector3 interpnt = *raystart + *raydir * param;

	for (int i = 3; i < numverts; i++)
	{
		Vector3 v(bver[bfac[i] * 3 / 2], bver[bfac[i] * 3 / 2 + 1], bver[bfac[i] * 3 / 2 + 2]);
		pnts[i] = v.transform(*worldmtx);
	}

	for (int i = 2; i < numverts - 1; i++)
		edges[i] = pnts[i + 1] - pnts[i];
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

GameObject *IsRayIntersectingObject(Vector3 *raystart, Vector3 *raydir, GameObject *o, Matrix *worldmtx)
{
	float d;
	Matrix objmtx = o->matrix;
	objmtx._41 = o->position.x;
	objmtx._42 = o->position.y;
	objmtx._43 = o->position.z;
	objmtx *= *worldmtx;
	if (o->mesh)
	{
		Mesh *m = o->mesh.get();
		float* vertices = (o->excChunk && o->excChunk->findSubchunk('LCHE')) ? ApplySkinToMesh(m, o->excChunk.get()) : m->vertices.data();
		for (size_t i = 0; i < m->getNumQuads(); i++)
			if (IsRayIntersectingFace(raystart, raydir, vertices, m->quadindices.data() + i * 4, 4, &objmtx))
				if ((d = (finalintersectpnt - campos).sqlen2xz()) < bestpickdist)
				{
					bestpickdist = d;
					bestpickobj = o;
					bestpickintersectionpnt = finalintersectpnt;
				}
		for(size_t i = 0; i < m->getNumTris(); i++)
			if (IsRayIntersectingFace(raystart, raydir, vertices, m->triindices.data() + i * 3, 3, &objmtx))
				if ((d = (finalintersectpnt - campos).sqlen2xz()) < bestpickdist)
				{
					bestpickdist = d;
					bestpickobj = o;
					bestpickintersectionpnt = finalintersectpnt;
				}
	}
	for (auto c = o->subobj.begin(); c != o->subobj.end(); c++)
		IsRayIntersectingObject(raystart, raydir, *c, &objmtx);
	return 0;
}

int main(int argc, char* argv[])
//int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, char *args, int winmode)
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

	auto zipPath = GuiUtils::OpenDialogBox("Scene ZIP archive\0*.zip\0\0\0", "zip", "Select a Scene ZIP archive (containing Pack.SPK)");
	if (zipPath.empty())
		exit(-1);
	g_scene.LoadSceneSPK(zipPath.string().c_str());

	bool appnoquit = true;
	InitWindow();

	ImGui::CreateContext(0);
	ImGui::GetStyle().WindowRounding = 7.0f;
	ImGui_ImplWin32_Init((void*)hWindow);
	ImGui_ImplOpenGL2_Init();
	lastfpscheck = GetTickCount();

	GlifyAllTextures();

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
				if (ImGui::IsKeyDown(VK_LEFT))
					cammove -= crab;
				if (ImGui::IsKeyDown(VK_RIGHT))
					cammove += crab;
				if (ImGui::IsKeyDown(VK_UP))
					cammove += ncd;
				if (ImGui::IsKeyDown(VK_DOWN))
					cammove -= ncd;
				if (ImGui::IsKeyDown('E'))
					cammove.y += 1;
				if (ImGui::IsKeyDown('D'))
					cammove.y -= 1;
				if (ImGui::IsKeyPressed('W'))
					wireframe = !wireframe;
				if (ImGui::IsKeyPressed('T'))
					rendertextures = !rendertextures;
			}
			campos += cammove * camspeed * (io.KeyShift ? 2.0f : 1.0f);
			if (io.MouseDown[0] && !io.WantCaptureMouse && !(io.KeyAlt || io.KeyCtrl))
			{
				camori.y += io.MouseDelta.x * 0.01f;
				camori.x += io.MouseDelta.y * 0.01f;
			}
			if (!io.WantCaptureMouse && viewobj)
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
				Matrix mtx = Matrix::getIdentity();
				IsRayIntersectingObject(&raystart, &raydir, viewobj, &mtx);
				if (io.KeyAlt) {
					if (bestpickobj && selobj)
						selobj->position = bestpickintersectionpnt;
				}
				else
					selobj = bestpickobj;
				cursorpos = bestpickintersectionpnt;
			}

			ImGui_ImplOpenGL2_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			IGMain();
			IGObjectTree();
			IGObjectInfo();
			IGDebugWindows();
			if(wndShowTextures) IGTextures();
			if(wndShowSounds) IGSounds();
			if (ImGui::BeginMainMenuBar()) {
				if (ImGui::BeginMenu("Tools")) {
					ImGui::MenuItem("Textures", nullptr, &wndShowTextures);
					ImGui::MenuItem("Sounds", nullptr, &wndShowSounds);
					ImGui::EndMenu();
				}
				IGDebugMenus();
				ImGui::EndMainMenuBar();
			}
			ImGui::EndFrame();

			BeginDrawing();
			glClearColor(0.5f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			glClearDepth(1.0f);
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			Matrix persp = Matrix::getLHPerspectiveMatrix(60.0f * (float)M_PI / 180.0f, (float)screen_width / (float)screen_height, camNearDist, camFarDist);
			glMultMatrixf(persp.v);
			Matrix lookat = Matrix::getLHLookAtViewMatrix(campos, campos + ncd, Vector3(0.0f, 1.0f, 0.0f));
			glMultMatrixf(lookat.v);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			float ovs = pow(2.0f, objviewscale);
			glScalef(ovs, ovs, ovs);

			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
			BeginMeshDraw();
			if (viewobj) {
				//glTranslatef(-viewobj->position.x, -viewobj->position.y, -viewobj->position.z);
				RenderObject(viewobj);
			}
			EndMeshDraw();

			if (renderExc && viewobj) {
				if (Chunk* pexc = g_scene.spkchk->findSubchunk('CXEP')) {
					glPointSize(5.0f);
					glBegin(GL_POINTS);
					auto renderAnim = [pexc](auto rec, GameObject* obj, const Matrix& prevmat) -> void {
						Matrix mat = obj->matrix * Matrix::getTranslationMatrix(obj->position) * prevmat;
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
						for (auto& child : obj->subobj) {
							rec(rec, child, mat);
						}
					};
					renderAnim(renderAnim, viewobj, Matrix::getIdentity());
					glEnd();
					glPointSize(1.0f);
				}
			}

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
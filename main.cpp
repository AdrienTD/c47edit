// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#define _USE_MATH_DEFINES
#include <cmath>
#include <ctime>
#include <functional>

#include "chunk.h"
#include "gameobj.h"
#include "global.h"
#include "texture.h"
#include "vecmat.h"
#include "video.h"
#include "window.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <commdlg.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl2.h"
#include "imgui/imgui_impl_win32.h"

GameObject *selobj = 0, *viewobj = 0;
float objviewscale = 0.0f;
Vector3 campos(0, 0, -50), camori(0,0,0);
float camspeed = 32;
bool wireframe = false;
bool findsel = false;
uint32_t framesincursec = 0, framespersec = 0, lastfpscheck;
Vector3 cursorpos(0, 0, 0);

GameObject *bestpickobj = 0;
float bestpickdist;
Vector3 bestpickintersectionpnt(0, 0, 0);

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

void IGOTNode(GameObject *o)
{
	bool op, colorpushed = 0;
	if (o == superroot)
		ImGui::SetNextTreeNodeOpen(true, ImGuiCond_Once);
	if (findsel)
		if (ObjInObj(selobj, o))
			ImGui::SetNextTreeNodeOpen(true, ImGuiCond_Always);
	if (o == viewobj) {
		colorpushed = 1;
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 1, 0, 1));
	}
	op = ImGui::TreeNodeEx(o, (o->subobj.empty() ? ImGuiTreeNodeFlags_Leaf : 0) | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ((o == selobj) ? ImGuiTreeNodeFlags_Selected : 0), "%s(0x%X)::%s", GetObjTypeString(o->type), o->type, o->name.c_str());
	if (colorpushed)
		ImGui::PopStyleColor();
	if (findsel)
		if (selobj == o)
			ImGui::SetScrollHere();
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
	IGOTNode(superroot);
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

#define swap_rb(a) ( (a & 0xFF00FF00) | ((a & 0xFF0000) >> 16) | ((a & 255) << 16) )

GameObject *objtogive = 0;

void IGObjectInfo()
{
	GameObject *nextobjtosel = 0;
	ImGui::SetNextWindowPos(ImVec2(1005, 3), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(270, 445), ImGuiCond_FirstUseEver);
	ImGui::Begin("Object information");
	if (!selobj)
		ImGui::Text("No object selected.");
	else {
		if (ImGui::Button("Duplicate"))
			if(selobj->root)
				DuplicateObject(selobj, selobj->root);
		ImGui::SameLine();
		bool wannadel = 0;
		if (ImGui::Button("Delete"))
			wannadel = 1;
		
		if (ImGui::Button("Set to be given"))
			objtogive = selobj;
		ImGui::SameLine();
		if (ImGui::Button("Give it here!"))
			if(objtogive)
				GiveObject(objtogive, selobj);
		
		if (ImGui::Button("Find in tree"))
			findsel = true;
		if (selobj->parent)
		{
			ImGui::SameLine();
			if (ImGui::Button("Select parent"))
				selobj = selobj->parent;
		}
		ImGui::Separator();

		char tb[256];
		strcpy_s(tb, selobj->name.c_str());
		if (ImGui::InputText("Name", tb, 255)) {
			selobj->name = tb;
		}
		ImGui::InputScalar("State", ImGuiDataType_U32, &selobj->state);
		ImGui::InputScalar("Type", ImGuiDataType_U32, &selobj->type);
		ImGui::InputScalar("Flags", ImGuiDataType_U32, &selobj->flags);
		ImGui::Separator();
		ImGui::InputScalar("PDBL offset", ImGuiDataType_U32, &selobj->pdbloff, 0, 0, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
		ImGui::InputScalar("PEXC offset", ImGuiDataType_U32, &selobj->pexcoff, 0, 0, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
		ImGui::DragFloat3("Position", &selobj->position.x);
		/*for (int i = 0; i < 3; i++) {
			ImGui::PushID(i);
			ImGui::DragFloat3((i==0) ? "Matrix" : "", selobj->matrix.m[i]);
			ImGui::PopID();
		}*/
		Vector3 rota = GetYXZRotVecFromMatrix(&selobj->matrix);
		rota *= 180.0f * M_1_PI;
		if (ImGui::DragFloat3("Orientation", &rota.x))
		{
			rota *= M_PI / 180.0f;
			Matrix my, mx, mz;
			CreateRotationYMatrix(&my, rota.y);
			CreateRotationXMatrix(&mx, rota.x);
			CreateRotationZMatrix(&mz, rota.z);
			selobj->matrix = mz * mx * my;
		}
		ImGui::Text("Num. references: %u", selobj->refcount);
		if (ImGui::CollapsingHeader("DBL"))
		{
			ImGui::InputScalar("Flags", ImGuiDataType_U32, &selobj->dblflags);
			int i = 0;
			for (auto e = selobj->dbl.begin(); e != selobj->dbl.end(); e++)
			{
				ImGui::PushID(i++);
				ImGui::Text("%1X", e->flags >> 4);
				ImGui::SameLine();
				switch (e->type)
				{
				case 1:
					ImGui::InputDouble("Double", &e->dbl); break;
				case 2:
					ImGui::InputFloat("Float", &e->flt); break;
				case 3:
				case 0xA:
				case 0xB:
				case 0xC:
				{
					char sb[10];
					sprintf(sb, "Int %X", e->type);
					ImGui::InputInt(sb, (int*)&e->u32); break;
				}
				case 4:
				case 5:
				{
					char sb[256];
					strncpy(sb, e->str, 255); sb[255] = 0;
					if (ImGui::InputText((e->type==5)?"Filename":"String", sb, 256))
					{
						free(e->str);
						e->str = strdup(sb);
					}
					break;
				}
				case 6:
					ImGui::Separator(); break;
				case 7:
					ImGui::Text("Data (%X): %u bytes", e->type, e->datsize); break;
				case 8:
					if (e->obj.valid()) {
						ImGui::Text("Object: %s", e->obj->name.c_str());
						if (ImGui::IsItemClicked())
							nextobjtosel = e->obj.get();
					}
					else
						ImGui::Text("Object: Invalid");
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("GameObject"))
						{
							e->obj = *(GameObject**)pl->Data;
						}
						ImGui::EndDragDropTarget();
					}
					break;
				case 9:
					ImGui::Text("Objlist: %u objects", e->nobjs);
					ImGui::ListBoxHeader("Objlist", ImVec2(0, 64));
					for (int i = 0; i < e->nobjs; i++)
					{
						ImGui::Text("%s", e->objlist[i]->name.c_str());
						if (ImGui::IsItemClicked())
							nextobjtosel = e->objlist[i].get();
						if (ImGui::BeginDragDropTarget())
						{
							if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("GameObject"))
							{
								e->objlist[i] = *(GameObject**)pl->Data;
							}
							ImGui::EndDragDropTarget();
						}
					}
					ImGui::ListBoxFooter();
					break;
				case 0x3F:
					ImGui::Text("End"); break;
				default:
					ImGui::Text("Unknown type %u", e->type); break;
				}
				ImGui::PopID();
			}
		}
		if (selobj->mesh)
			if(ImGui::CollapsingHeader("Mesh"))
		{
			//ImGui::Separator();
			//ImGui::Text("Mesh");
			ImVec4 c = ImGui::ColorConvertU32ToFloat4(swap_rb(selobj->color));
			if (ImGui::ColorEdit4("Color", &c.x, 0))
				selobj->color = swap_rb(ImGui::ColorConvertFloat4ToU32(c));
			ImGui::Text("Vertex start index: %u", selobj->mesh->vertstart);
			ImGui::Text("Quad start index:   %u", selobj->mesh->quadstart);
			ImGui::Text("Tri start index:    %u", selobj->mesh->tristart);
			ImGui::Text("Vertex count: %u", selobj->mesh->numverts);
			ImGui::Text("Quad count:   %u", selobj->mesh->numquads);
			ImGui::Text("Tri count:    %u", selobj->mesh->numtris);
			ImGui::Text("FTXO offset: 0x%X", selobj->mesh->ftxo);
		}
		if (selobj->light)
			if(ImGui::CollapsingHeader("Light"))
		{
			//ImGui::Separator();
			//ImGui::Text("Light");
			char s[] = "Param ?\0";
			for (int i = 0; i < 7; i++)
			{
				s[6] = '0' + i;
				ImGui::InputScalar(s, ImGuiDataType_U32, &selobj->light->param[i], 0, 0, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
			}
		}
		if (wannadel)
		{
			if (selobj->refcount > 0)
				warn("It's not possible to remove an object that is referenced by other objects!");
			else {
				RemoveObject(selobj);
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
		char newfn[300]; newfn[299] = 0;
		_splitpath(lastspkfn.c_str(), 0, 0, newfn, 0);
		strcat(newfn, ".zip");
		char *s = strrchr(newfn, '@');
		if (s)
			s += 1;
		else
			s = newfn;

		OPENFILENAME ofn; char zipfilename[1024]; // = "lol"; //zipfilename[0] = 0;
		strcpy(zipfilename, s);
		memset(&ofn, 0, sizeof(OPENFILENAME));
		ofn.lStructSize = sizeof(OPENFILENAME);
		ofn.hwndOwner = hWindow;
		ofn.hInstance = GetModuleHandle(0);
		ofn.lpstrFilter = "Scene ZIP archive\0*.zip\0\0\0";
		ofn.lpstrFile = zipfilename;
		ofn.nMaxFile = 1023;
		ofn.lpstrTitle = "Save Scene ZIP archive as...";
		ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
		ofn.lpstrDefExt = "zip";
		if (GetSaveFileName(&ofn))
			SaveSceneSPK(zipfilename);
	}
	ImGui::SameLine();
	if(ImGui::Button("About..."))
		MessageBox(hWindow, "c47edit\nUnofficial scene editor for \"Hitman: Codename 47\"\n\n"
			"(C) 2018 AdrienTD\nLicensed under the GPL 3.\nSee LICENSE file for details.\n\n"
			"3rd party libraries used:\n- Dear ImGui (MIT license)\n- Miniz (MIT license)\nSee LICENSE_* files for copyright and licensing of these libraries.", "c47edit", 0);
	//ImGui::DragFloat("Scale", &objviewscale, 0.1f);
	ImGui::DragFloat("Cam speed", &camspeed, 0.1f);
	ImGui::DragFloat3("Cam pos", &campos.x, 0.1f);
	ImGui::DragFloat2("Cam ori", &camori.x, 0.1f);
	ImGui::DragFloat3("Cursor pos", &cursorpos.x);
	ImGui::Checkbox("Wireframe", &wireframe);
	ImGui::SameLine();
	ImGui::Checkbox("Textured", &rendertextures);
	ImGui::Text("FPS: %u", framespersec);
	ImGui::End();
}

uint32_t curtexid = 0;

void IGTest()
{
	ImGui::Begin("Debug/Test");
	if (ImGui::Button("ReadTextures()"))
		ReadTextures();
	static const int one = 1;
	ImGui::InputScalar("Texture ID", ImGuiDataType_U32, &curtexid, &one);
	auto l = texmap.lower_bound(curtexid);
	if(ImGui::Button("Next"))
		if (l != texmap.end()) {
			auto ln = std::next(l);
			if (ln != texmap.end())
				curtexid = ln->first;
		}
	auto t = texmap.find(curtexid);
	if (t != texmap.end())
		ImGui::Image(t->second, ImVec2(256, 256));
	else
		ImGui::Text("Texture %u not found.", curtexid);

//#include "unused/moredebug.inc"

	ImGui::End();
}

GameObject* FindObjectNamed(const char *name, GameObject *sup = rootobj)
{
	if (!strcmp(sup->name.c_str(), name))
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
		o->mesh->draw();
	}
	for (auto e = o->subobj.begin(); e != o->subobj.end(); e++)
		RenderObject(*e);
	glPopMatrix();
}

Vector3 finalintersectpnt = Vector3(0, 0, 0);

bool IsRayIntersectingFace(Vector3 *raystart, Vector3 *raydir, int startvertex, int startface, int numverts, Matrix *worldmtx)
{
	uint16_t *bfac = (uint16_t*)pfac->maindata + startface;
	float *bver = (float*)pver->maindata + startvertex;

	Vector3 *pnts = new Vector3[numverts];
	for (int i = 0; i < 3; i++)
	{
		Vector3 v(bver[bfac[i] * 3 / 2], bver[bfac[i] * 3 / 2 + 1], bver[bfac[i] * 3 / 2 + 2]);
		TransformVector3(&pnts[i], &v, worldmtx);
	}

	Vector3 *edges = new Vector3[numverts];
	for (int i = 0; i < 2; i++)
		edges[i] = pnts[i + 1] - pnts[i];

	Vector3 planenorm = edges[1].cross(edges[0]);
	float planeord = -planenorm.dot(pnts[0]);

	float planenorm_dot_raydir = planenorm.dot(*raydir);
	if (planenorm_dot_raydir >= 0) goto irifend;

	float param = -(planenorm.dot(*raystart) + planeord) / planenorm_dot_raydir;
	if (param < 0) goto irifend;

	Vector3 interpnt = *raystart + *raydir * param;

	for (int i = 3; i < numverts; i++)
	{
		Vector3 v(bver[bfac[i] * 3 / 2], bver[bfac[i] * 3 / 2 + 1], bver[bfac[i] * 3 / 2 + 2]);
		TransformVector3(&pnts[i], &v, worldmtx);
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
			goto irifend;
	}

	finalintersectpnt = interpnt;
	delete[] pnts;
	delete[] edges;
	return true;

irifend:
	delete[] pnts;
	delete[] edges;
	return false;
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
		Mesh *m = o->mesh;
		for (int i = 0; i < m->numquads; i++)
			if (IsRayIntersectingFace(raystart, raydir, m->vertstart, m->quadstart + i*4, 4, &objmtx))
				if ((d = (finalintersectpnt - campos).sqlen2xz()) < bestpickdist)
				{
					bestpickdist = d;
					bestpickobj = o;
					bestpickintersectionpnt = finalintersectpnt;
				}
		for(int i = 0; i < m->numtris; i++)
			if (IsRayIntersectingFace(raystart, raydir, m->vertstart, m->tristart  + i*3, 3, &objmtx))
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

	OPENFILENAME ofn; char zipfilename[1024]; zipfilename[0] = 0;
	memset(&ofn, 0, sizeof(OPENFILENAME));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hInstance = GetModuleHandle(0);
	ofn.lpstrFilter = "Scene ZIP archive\0*.zip\0\0\0";
	ofn.lpstrFile = zipfilename;
	ofn.nMaxFile = 1023;
	ofn.lpstrTitle = "Select a Scene ZIP archive (containing Pack.SPK)";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	ofn.lpstrDefExt = "zip";
	if (!GetOpenFileName(&ofn))
		exit(-1);
	LoadSceneSPK(zipfilename);

	bool appnoquit = true;
	InitWindow();

	ImGui::CreateContext(0);
	ImGui_ImplWin32_Init((void*)hWindow);
	ImGui_ImplOpenGL2_Init();
	lastfpscheck = GetTickCount();

	while (appnoquit = HandleWindow())
	{
		if (win_minimized)
			Sleep(100);
		else
		{
			Matrix m1, m2, crm; Vector3 cd(0, 0, 1), ncd;
			CreateRotationXMatrix(&m1, camori.x);
			CreateRotationYMatrix(&m2, camori.y);
			MultiplyMatrices(&crm, &m1, &m2);
			//CreateRotationYXZMatrix(&crm, camori.y, camori.x, 0);
			TransformVector3(&ncd, &cd, &crm);
			Vector3 crabnn;
			Vec3Cross(&crabnn, &Vector3(0, 1, 0), &ncd);
			Vector3 crab = crabnn.normal();

			Vector3 cammove(0, 0, 0);
			ImGuiIO& io = ImGui::GetIO();
			if (!io.WantCaptureKeyboard)
			{
				if (io.KeysDown[VK_LEFT])
					cammove -= crab;
				if (io.KeysDown[VK_RIGHT])
					cammove += crab;
				if (io.KeysDown[VK_UP])
					cammove += ncd;
				if (io.KeysDown[VK_DOWN])
					cammove -= ncd;
				if (io.KeysDown['E'])
					cammove.y += 1;
				if (io.KeysDown['D'])
					cammove.y -= 1;
				if (ImGui::IsKeyPressed('W'))
					wireframe = !wireframe;
				if (ImGui::IsKeyPressed('T'))
					rendertextures = !rendertextures;
			}
			campos += cammove * camspeed * (io.KeyShift ? 2 : 1);
			if (io.MouseDown[0] && !io.WantCaptureMouse && !(io.KeyAlt || io.KeyCtrl))
			{
				camori.y += io.MouseDelta.x * 0.01f;
				camori.x += io.MouseDelta.y * 0.01f;
			}
			if (!io.WantCaptureMouse && viewobj)
			if (io.MouseClicked[1] || (io.MouseClicked[0] && (io.KeyAlt || io.KeyCtrl)))
			{
				Matrix lookat, persp;
				CreatePerspectiveMatrix(&persp, 60 * 3.141 / 180, screen_width / screen_height, 1, 10000);
				CreateLookAtLHViewMatrix(&lookat, &campos, &(campos + ncd), &Vector3(0, 1, 0));
				Matrix matView = lookat * persp;

				Vector3 raystart, raydir;
				float ys = 1 / tan(60 * 3.141 / 180 / 2);
				float xs = ys / ((float)screen_width / (float)screen_height);
				ImVec2 mspos = ImGui::GetMousePos();
				float msx = mspos.x * 2.0f / (float)screen_width - 1.0f;
				float msy = mspos.y * 2.0f / (float)screen_height - 1.0f;
				Vector3 hi = ncd.cross(crab);
				raystart = campos + ncd + crab * (msx / xs) - hi * (msy / ys);
				raydir = raystart - campos;

				bestpickobj = 0;
				bestpickdist = HUGE_VAL; //100000000000000000.0f;
				Matrix mtx; CreateIdentityMatrix(&mtx);
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
//#if 1
			IGTest();
//#endif
			ImGui::EndFrame();

			BeginDrawing();
			glClearColor(0.5f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			glClearDepth(1.0f);
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			Matrix lookat, persp;
			CreatePerspectiveMatrix(&persp, 60 * 3.141 / 180, (float)screen_width / (float)screen_height, 1, 10000);
			glMultMatrixf(persp.v);
			CreateLookAtLHViewMatrix(&lookat, &campos, &(campos + ncd), &Vector3(0, 1, 0));
			glMultMatrixf(lookat.v);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			float ovs = pow(2, objviewscale);
			glScalef(ovs, ovs, ovs);

			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
			BeginMeshDraw();
			if (viewobj) {
				//glTranslatef(-viewobj->position.x, -viewobj->position.y, -viewobj->position.z);
				RenderObject(viewobj);
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
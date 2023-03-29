// c47edit - Scene editor for HM C47
// Copyright (C) 2018-2022 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "classInfo.h"
#include "gameobj.h"

#include <charconv>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

// Iterates through the members in a member list string (such as "int number=47;char* name;ZGEOMREF weapon;")
struct MemberDecoder {
	const char* _string;
	std::string_view type, name, value; int arrayCount = 0;
	
	MemberDecoder(const char* string) : _string(string) {}

	// Obtains the next data member in the type, name, value and arrayCount variables. Returns true if successful, false if end reached.
	bool next() {
		const char* ptr = _string;
		auto skipWhitespace = [&ptr]() {while (*ptr && *ptr == ' ') ++ptr; };
		auto skipWord = [&ptr]() {while (*ptr && *ptr != ' ' && *ptr != ';' && *ptr != '=' && *ptr != '[') ++ptr; };
		skipWhitespace();
		if (!*ptr)
			return false;
		const char* b_type = ptr;
		skipWord();
		type = std::string_view{ b_type, (size_t)(ptr - b_type) };
		skipWhitespace();
		const char* b_name = ptr;
		skipWord();
		name = { b_name, (size_t)(ptr - b_name) };
		skipWhitespace();
		arrayCount = 1;
		if (*ptr == '[') {
			++ptr;
			const char* b_count = ptr;
			while (*ptr && *ptr != ']') ++ptr;
			std::from_chars(b_count, ptr, arrayCount);
			++ptr;
			skipWhitespace();
		}
		if (*ptr == '=') {
			++ptr;
			skipWhitespace();
			const char* b_val = ptr;
			if (*ptr == '{') {
				while (*ptr && *ptr != '}') ++ptr;
				++ptr;
			}
			else {
				skipWord();
			}
			value = { b_val, (size_t)(ptr - b_val) };
		}
		else {
			value = {};
		}
		while (*ptr && *ptr != ';') ++ptr;
		++ptr;
		_string = ptr;
		return true;
	}
};

nlohmann::json g_classInfoJson;
std::map<int, const nlohmann::json*> g_classInfo_idJsonMap; // ID -> Json map
std::map<std::string, int> g_classInfo_stringIdMap;         // Class name -> ID map
std::map<std::string, const nlohmann::json*> g_cpntJsonMap; // Component name -> Json map

std::map<std::string, std::vector<ClassInfo::ClassMember>> g_classMemberLists;

static std::string strUpper(std::string_view sv) {
	std::string str = std::string{ sv };
	for (char& c : str)
		if (c >= 'a' && c <= 'z')
			c = c - 'a' + 'A';
	return str;
}

// Initialize the class info from the file
void ClassInfo::ReadClassInfo()
{
	std::ifstream file("classes.json");
	file >> g_classInfoJson;

	for (const auto& ci : g_classInfoJson.at("geoms")) {
		int id = ci.at("num2").get<int>() & 0xFFFFF;
		g_classInfo_idJsonMap[id] = &ci;
		const std::string& name = ci.at("name").get_ref<const std::string&>();
		g_classInfo_stringIdMap[name] = id;
		g_classMemberLists[name] = ProcessClassMemberListString(ci.at("members").get_ref<const std::string&>());
	}
	for (const auto& cpnt : g_classInfoJson.at("components")) {
		std::string name = cpnt.at("infoClassName").get_ref<const std::string&>().substr(1);
		g_cpntJsonMap[name] = &cpnt;
		g_classMemberLists[name] = ProcessClassMemberListString(cpnt.at("members").get_ref<const std::string&>());
	}
}

// Get name of class from ID
const char* ClassInfo::GetObjTypeString(int typeId)
{
	return g_classInfo_idJsonMap.at(typeId)->at("name").get_ref<const std::string&>().c_str();
}

uint16_t ClassInfo::GetObjTypeCategory(int typeId)
{
	return g_classInfo_idJsonMap.at(typeId)->at("num2").get<uint32_t>() >> 16;
}

// Parse a string containing a list of class members
std::vector<ClassInfo::ClassMember> ClassInfo::ProcessClassMemberListString(const std::string& membersString)
{
	std::vector<ClassMember> members;
	MemberDecoder dec{ membersString.c_str() };
	while (dec.next()) {
		ClassMember& mem = members.emplace_back();
		mem.type = strUpper(dec.type);
		mem.name = dec.name;
		if (dec.value.size() >= 1 && dec.value[0] == '{') {
			const char* ptr = dec.value.data() + 1;
			mem.valueChoices.emplace_back();
			while (*ptr != '}') {
				if (*ptr == ',')
					mem.valueChoices.emplace_back();
				else
					mem.valueChoices.back().push_back(*ptr);
				++ptr;
			}
		}
		else {
			mem.defaultValue = dec.value;
		}
		mem.arrayCount = dec.arrayCount;
		if (mem.type.size() >= 1 && mem.type[0] == '@') {
			mem.isProtected = true;
			mem.type = mem.type.substr(1);
		}
	}
	return members;
}

// Get an array of members for the DBL (arrays repeat the same member in the list)
void ClassInfo::AddDBLMemberInfo(std::vector<ClassInfo::ObjectMember>& members, const std::vector<ClassInfo::ClassMember>& memlist)
{
	static const ClassMember emptyMember = { "", "" };
	for (const ClassMember& mem : memlist) {
		if (mem.arrayCount == 1)
			members.emplace_back(&mem, -1);
		else {
			for (int i = 0; i < mem.arrayCount; ++i)
				members.emplace_back(&mem, i);
		}
	}
	members.emplace_back(&emptyMember);
}

// Return a list of names of all DBL members of the object
std::vector<ClassInfo::ObjectMember> ClassInfo::GetMemberNames(GameObject* obj)
{
	static const ClassMember emptyMember = { "", "" };
	static const ClassMember initialMembers[3] = { {"CHAR*", "Components"}, {"ENUM", "Create", "", {"ROOT", "CLIP"}}, {"SCRIPT", "ZGeomScript"}}; // some objects might have less or more initial members...
	std::vector<ClassInfo::ObjectMember> members = { {&initialMembers[0]}, {&initialMembers[1]}, {&initialMembers[2]}, {&emptyMember} };
	auto onClass = [&members](const auto& rec, const nlohmann::json& cl) -> void {
		if (cl.at("name") == "ZGEOM")
			return;
		const auto& parent = cl.at("type").get_ref<const std::string&>();
		rec(rec, *g_classInfo_idJsonMap.at(g_classInfo_stringIdMap.at(parent))); // recursive call to parent class
		const auto& membersString = cl.at("members").get_ref<const std::string&>();
		const auto& memlist = g_classMemberLists.at(cl.at("name").get_ref<const std::string&>());
		AddDBLMemberInfo(members, memlist);
	};
	onClass(onClass, *g_classInfo_idJsonMap.at(obj->type));

	if (!obj->dbl.entries.empty()) {
		const auto& cpntList = std::get<std::string>(obj->dbl.entries[0].value);
		const char* ptr = cpntList.c_str(), *beg;
		auto skipWhitespace = [&ptr]() {while (*ptr && *ptr == ' ') ++ptr; };
		auto skipWord = [&ptr]() {while (*ptr && *ptr != ' ' && *ptr != ',') ++ptr; };
		auto nextElem = [&ptr]() {while (*ptr && *ptr != ',') ++ptr; if (*ptr) ++ptr; };
		while (*ptr) {
			skipWhitespace();
			beg = ptr;
			skipWord();
			auto cpntName = std::string_view(beg, (size_t)(ptr - beg));
			nextElem();
			skipWhitespace();

			const auto& memlist = g_classMemberLists.at(std::string(cpntName));
			AddDBLMemberInfo(members, memlist);
		}
	}

	return members;
}

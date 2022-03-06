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
			skipWord();
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

// Initialize the class info from the file
void ClassInfo::ReadClassInfo()
{
	std::ifstream file("classes.json");
	file >> g_classInfoJson;
	for (const auto& ci : g_classInfoJson.at("geoms")) {
		int id = ci.at("num2").get<int>() & 0xFFFFF;
		g_classInfo_idJsonMap[id] = &ci;
		g_classInfo_stringIdMap[ci.at("name").get_ref<const std::string&>()] = id;
	}
	for (const auto& cpnt : g_classInfoJson.at("components")) {
		g_cpntJsonMap[cpnt.at("infoClassName").get_ref<const std::string&>().substr(1)] = &cpnt;
	}
}

// Get name of class from ID
const char* ClassInfo::GetObjTypeString(int typeId)
{
	return g_classInfo_idJsonMap.at(typeId)->at("name").get_ref<const std::string&>().c_str();
}

// Return a list of names of all DBL members of the object
std::vector<std::string> ClassInfo::GetMemberNames(GameObject* obj)
{
	std::vector<std::string> members = { "Components", "Head1", "Head2", "" }; // some objects might have less or more initial members...
	auto onClass = [&members](const auto& rec, const nlohmann::json& cl) -> void {
		if (cl.at("name") == "ZGEOM")
			return;
		const auto& parent = cl.at("type").get_ref<const std::string&>();
		rec(rec, *g_classInfo_idJsonMap.at(g_classInfo_stringIdMap.at(parent))); // recursive call to parent class
		const auto& membersString = cl.at("members").get_ref<const std::string&>();
		MemberDecoder dec{ membersString.c_str() };
		while (dec.next()) {
			if (dec.arrayCount == 1)
				members.emplace_back(dec.name);
			else {
				for (int i = 0; i < dec.arrayCount; ++i)
					members.push_back(std::string(dec.name) + '[' + std::to_string(i) + ']');
			}
		}
		members.emplace_back();
	};
	onClass(onClass, *g_classInfo_idJsonMap.at(obj->type));

	if (!obj->dbl.empty()) {
		const auto& cpntList = std::get<std::string>(obj->dbl[0].value);
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

			const auto& membersString = g_cpntJsonMap.at(std::string(cpntName))->at("members").get_ref<const std::string&>();
			MemberDecoder dec{ membersString.c_str() };
			while (dec.next()) {
				if (dec.arrayCount == 1)
					members.emplace_back(dec.name);
				else {
					for (int i = 0; i < dec.arrayCount; ++i)
						members.push_back(std::string(dec.name) + '[' + std::to_string(i) + ']');
				}
			}
			members.emplace_back();
		}
	}

	return members;
}

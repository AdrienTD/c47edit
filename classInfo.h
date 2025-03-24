// c47edit - Scene editor for HM C47
// Copyright (C) 2018-2022 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

struct GameObject;

extern std::map<std::string, int> g_classInfo_stringIdMap;         // Class name -> ID map

namespace ClassInfo {
	// Initialize the class info from the file
	void ReadClassInfo();

	// Get name of class from ID
	const char *GetObjTypeString(int typeId);

	// Get class category flags
	uint16_t GetObjTypeCategory(int typeId);

	struct ClassMember {
		std::string type;
		std::string name;
		std::string defaultValue;
		std::vector<std::string> valueChoices;
		int arrayCount = 1;
		bool isProtected = false;
	};

	struct ObjectMember {
		const ClassMember* info;
		int arrayIndex;
		ObjectMember(const ClassMember* info, int arrayIndex = -1) : info(info), arrayIndex(arrayIndex) {}
	};

	struct ObjectComponent {
		std::string name;
		int number;
		int startIndex;
		int numElements;
	};

	// Parse a string containing a list of class members
	std::vector<ClassMember> ProcessClassMemberListString(const std::string& membersString);

	// Get an array of members for the DBL (arrays repeat the same member in the list)
	void AddDBLMemberInfo(std::vector<ObjectMember>& members, const std::vector<ClassMember>& memlist);

	// Return a list of names of all DBL members of the object
	std::vector<ObjectMember> GetMemberNames(GameObject* obj, std::vector<ObjectComponent>* outComponents = nullptr);
}

extern std::map<std::string, std::vector<ClassInfo::ClassMember>> g_classMemberLists;

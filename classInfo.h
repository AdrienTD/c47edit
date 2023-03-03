// c47edit - Scene editor for HM C47
// Copyright (C) 2018-2022 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <string>
#include <string_view>
#include <vector>

struct GameObject;

namespace ClassInfo {
	// Initialize the class info from the file
	void ReadClassInfo();

	// Get name of class from ID
	const char *GetObjTypeString(int typeId);

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

	// Return a list of names of all DBL members of the object
	std::vector<ObjectMember> GetMemberNames(GameObject* obj);
}

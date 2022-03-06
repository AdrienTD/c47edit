// c47edit - Scene editor for HM C47
// Copyright (C) 2018-2022 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <string>
#include <vector>

struct GameObject;

namespace ClassInfo {
	// Initialize the class info from the file
	void ReadClassInfo();

	// Get name of class from ID
	const char *GetObjTypeString(int typeId);

	// Return a list of names of all DBL members of the object
	std::vector<std::string> GetMemberNames(GameObject* obj);
}

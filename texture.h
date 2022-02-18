#pragma once

#include <cstdint>
#include <map>

extern std::map<uint32_t, void*> texmap;

void ReadTextures();

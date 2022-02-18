// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#define WIN32_LEAN_AND_MEAN
#define _USE_MATH_DEFINES

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdint>
#include <cmath>

#ifndef APP_VERSION
#define APP_VERSION "DEV"
#endif

void ferr(const char *str);
void warn(const char *str);

#include "vecmat.h"
#include "chunk.h"
#include "gameobj.h"
#include "window.h"
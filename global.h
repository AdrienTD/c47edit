#define WIN32_LEAN_AND_MEAN
#define _USE_MATH_DEFINES

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdint>
#include <cmath>

typedef unsigned int uint;

void ferr(char *str);
void warn(char *str);

#include "vecmat.h"
#include "chunk.h"
#include "gameobj.h"
#include "window.h"
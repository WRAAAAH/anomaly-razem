#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

extern "C" {
#include "enet/enet.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

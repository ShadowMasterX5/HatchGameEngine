#ifndef STANDARDLIBS_H
#define STANDARDLIBS_H

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include <unordered_map>
#include <algorithm>
#include <string>
#include <vector>
#include <deque>
#include <stack>
#include <csetjmp>

#if WIN32
    #define snprintf _snprintf
    #undef __useHeader
    #undef __on_failure
#endif

using namespace std;

enum class Platforms {
    Default,
    Windows,
    MacOSX,
    Linux,
    Ubuntu,
    Switch,
    Playstation,
    Xbox,
    Android,
    iOS,
};

#ifndef __OBJC__

#endif

#define MAX_SCENE_VIEWS 8
#define MAX_PALETTE_COUNT 32
#define MAX_DEFORM_LINES 0x400
#define MAX_FRAMEBUFFER_HEIGHT 4096

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int8_t Sint8;
typedef int16_t Sint16;
typedef int32_t Sint32;
typedef int64_t Sint64;

#ifdef IOS
#define NEW_STRUCT_MACRO(n) (n)
#else
#define NEW_STRUCT_MACRO(n) n
#endif
#endif // STANDARDLIBS_H

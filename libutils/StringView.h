#pragma once

#include "libutils\Defs.h"

typedef const char* StringView;
typedef char* MutableStringView;
StringView stringIntern(StringView view);
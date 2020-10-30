#define _AMD64_ 1
#define _FI __forceinline
extern "C" int _fltused = 0;

#include <ntifs.h>
#include <intrin.h>

#pragma comment(lib, "ntoskrnl.lib")
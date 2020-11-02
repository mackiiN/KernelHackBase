//#pragma comment(linker, "/MERGE:.rdata=INIT")
#pragma comment(linker, "/MERGE:.pdata=INIT")

#define _AMD64_ 1
#define _KERNEL_MODE 1
#define _FI __forceinline
extern "C" int _fltused;

#include <ntifs.h>
#include <intrin.h>
#include <windef.h>

//#pragma comment(lib, "ntoskrnl.lib")
//#define DBG 1
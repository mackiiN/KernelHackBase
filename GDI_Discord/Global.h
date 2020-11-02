

//#pragma comment(linker, "/MERGE:.rdata=INIT") //hz
#pragma comment(linker, "/MERGE:.pdata=INIT")

#define _AMD64_ 1
#define _KERNEL_MODE 1
#define _FI __forceinline
extern "C" int _fltused = 0;

#include <ntifs.h>
#include <intrin.h>
#include <windef.h>

#include "Internals.h"
#include "CryptSTR.h"
#include "CRT.h"
#include "HideImport.h"
#include "Helpers.h"
#include "KGDI.h"

#pragma comment(lib, "ntoskrnl.lib")
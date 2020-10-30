#include "Global.h"

#include "Internals.h"
#include "CRT.h"
#include "Helpers.h"

NTSTATUS DriverEntry(PVOID a1, PVOID KBase)
{
	//get pte base
	//ULONG64 PTE = (ULONG64)FindPatternSect(KBase, ".text", "48 23 C8 48 B8 ? ? ? ? ? ? ? ? 48 03 C1 C3");
	//if (PTE) Global::PTE_Base = (*(ULONG64*)(PTE + 5)); else Global::PTE_Base = (0xFFFFF68000000000);

	//SetupFakeThread(KBase);

	return STATUS_SUCCESS;
}
#include "Global.h"
#include "Internals.h"
#include "CryptSTR.h"
#include "CRT.h"
#include "Helpers.h"
#include "KGDI.h"

class DiscordTexture
{
private:
	UCHAR Stub[12];

public:
	ULONG Width;
	ULONG Height;
	UCHAR Texture[];

	_FI ULONG GetTextureSize() {
		return this->Width * this->Height * 4;
	}

	_FI void UpdateFrameCount() {
		ULONG& FrameCount = *(ULONG*)((ULONG_PTR)this + 4);
		FrameCount++;
	}
};

//tmp hook data
PVOID* xKdEnumerateDebuggingDevicesPtr;
PVOID xKdEnumerateDebuggingDevicesVal;

//create thread meme
bool SetupKernelThread(PVOID KBase, PVOID ThreadStartAddr)
{
	//get thread fake start address
	PVOID hMsVCRT = nullptr;
	auto Process = GetProcessWModule(E("explorer.exe"), E("msvcrt"), &hMsVCRT);
	auto FakeStartAddr = (PUCHAR)GetProcAdress(hMsVCRT, E("_endthreadex")) + 0x30;

	//get usermode func
	auto Var = UAlloc(0x1000); HANDLE Thread = nullptr;
	auto hNtdll = GetUserModuleBase(Process, E("ntdll"));
	auto CallBack = GetProcAdress(hNtdll, E("NtQueryAuxiliaryCounterFrequency"));

	//set kernel hook
	xKdEnumerateDebuggingDevicesPtr = (PVOID*)RVA((ULONG64)KeQueryAuxiliaryCounterFrequency + 4, 7);
	xKdEnumerateDebuggingDevicesVal = _InterlockedExchangePointer(xKdEnumerateDebuggingDevicesPtr, ThreadStartAddr);

	//create new thread
	CLIENT_ID Cid;
	RtlCreateUserThread(ZwCurrentProcess(), nullptr, false, 0, 0, 0, CallBack, Var, &Thread, &Cid);

	if (Thread)
	{
		//close useless handle
		ZwClose(Thread);

		//spoof thread start address
		PETHREAD EThread;
		PsLookupThreadByThreadId(Cid.UniqueThread, &EThread);
		auto StartAddrOff = *(USHORT*)(FindPatternSect(KBase, E("PAGE"), E("48 89 86 ? ? ? ? 48 8B 8C")) + 3);
		*(PVOID*)((ULONG64)EThread + StartAddrOff/*Win32StartAddress*/) = FakeStartAddr;
		ObfDereferenceObject(EThread);

		//wait exec kernel callback
		while (xKdEnumerateDebuggingDevicesPtr && 
			   xKdEnumerateDebuggingDevicesVal) {
			Sleep(10);
		}
	}

	//cleanup
	UFree(Var);
	DetachFromProcess(Process);

	//ret create status
	return (bool)Thread;
}

//meme thread
NTSTATUS FakeThread()
{
	//disable apc
	KeEnterGuardedRegion();

	//unhook kernel hook
	_InterlockedExchangePointer(xKdEnumerateDebuggingDevicesPtr, xKdEnumerateDebuggingDevicesVal);
	xKdEnumerateDebuggingDevicesPtr = nullptr; xKdEnumerateDebuggingDevicesVal = nullptr;

	//create gui thread context
	auto hNtdll = GetUserModuleBase(IoGetCurrentProcess(), E("user32"));
	auto CallBack = GetProcAdress(hNtdll, E("GetForegroundWindow"));
	CallUserMode(CallBack);

	//get target process
	NewScan: DiscordTexture* DBuff = nullptr;
	auto TargetProc = GetProcessWModule("ConsoleApplication8.exe", E("DiscordHook64"), nullptr);

	//find discord texture
	ULONG64 Addr = 0; do {
		MEMORY_BASIC_INFORMATION MemInfo{}; SIZE_T RetSize;
		if (NT_SUCCESS(ZwQueryVirtualMemory(ZwCurrentProcess(), (PVOID)Addr, MemoryBasicInformation, &MemInfo, 48, &RetSize))) {
			if ((MemInfo.Type & (MEM_COMMIT | MEM_MAPPED)) &&
				(MemInfo.Protect & PAGE_READWRITE) &&
				(MemInfo.RegionSize == 0x3201000)) {
				DBuff = (DiscordTexture*)MemInfo.AllocationBase;
			} Addr += MemInfo.RegionSize;
		} else Addr += PAGE_SIZE;
	} while (Addr < (ULONG_PTR)0x7FFFFFFEFFFFi64);

	//check found buffer
	if (!DBuff) {
		DetachFromProcess(TargetProc);
		goto NewScan;
	}

	//alloc render instance
	Render gRender{};

	//your cheat
	while (true)
	{
		gRender.NewFrame(DBuff->Width, DBuff->Height);

		
		gRender.RoundedRectangle(100, 100, 200, 200, RGB(0, 255, 0), 8.f/*, 100*/);

		gRender.EndFrame(DBuff->Texture);
		DBuff->UpdateFrameCount();

		Sleep(1);
	}

	//enable all apc
	KeLeaveGuardedRegion();

	//lol return 1!!
	return STATUS_NOT_IMPLEMENTED;
}

//driver entry point
NTSTATUS DriverEntry(PVOID a1, PVOID KBase)
{
	//disable apc
	KeEnterGuardedRegion();

	//create kernel usermode thread
	SetupKernelThread(KBase, FakeThread);

	//enable all apc
	KeLeaveGuardedRegion();

	return STATUS_SUCCESS;
}
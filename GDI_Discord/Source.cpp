#include "Global.h"

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

#define GameProc "ConsoleApplication8.exe"

//meme thread
NTSTATUS FakeThread()
{
	//disable apc
	ImpCall(KeEnterGuardedRegion);

	//unhook kernel hook
	_InterlockedExchangePointer(xKdEnumerateDebuggingDevicesPtr, xKdEnumerateDebuggingDevicesVal);
	xKdEnumerateDebuggingDevicesPtr = nullptr; xKdEnumerateDebuggingDevicesVal = nullptr;

	//create gui thread context
	auto hNtdll = GetUserModuleBase(ImpCall(IoGetCurrentProcess), E("user32"));
	auto CallBack = GetProcAdress(hNtdll, E("GetForegroundWindow"));
	CallUserMode(CallBack);

	//get target process
	NewScan: DiscordTexture* DBuff = nullptr;
	auto TargetProc = GetProcessWModule(E(GameProc), E("DiscordHook64"), nullptr);

	//find discord texture
	ULONG64 Addr = 0; do {
		MEMORY_BASIC_INFORMATION MemInfo{}; SIZE_T RetSize;
		if (NT_SUCCESS(ImpCall(ZwQueryVirtualMemory, ZwCurrentProcess(), (PVOID)Addr, MemoryBasicInformation, &MemInfo, 48, &RetSize))) {
			if ((MemInfo.Type & (MEM_COMMIT | MEM_MAPPED)) &&
				(MemInfo.Protect & PAGE_READWRITE) &&
				(MemInfo.RegionSize == 0x3201000)) {
				DBuff = (DiscordTexture*)MemInfo.AllocationBase;
			} Addr += MemInfo.RegionSize;
		} else Addr += PAGE_SIZE;
	} while (Addr < (ULONG_PTR)0x7FFFFFFEFFFFi64);

	//check buffer
	if (!DBuff) {
		DetachFromProcess(TargetProc);
		Sleep(1);
		goto NewScan;
	}

	//don't unload discord memory
	ImpCall(MmSecureVirtualMemory, DBuff, 0x3201000, PAGE_READWRITE);

	//alloc render instance
	Render gRender{};

	//your cheat
	while (true)
	{
		//acquire process lock & check process died
		if (ImpCall(PsAcquireProcessExitSynchronization, TargetProc)) {
			DetachFromProcess(TargetProc);
			break;
		}

		//discord ne prosral`sa
		if (!DBuff->Width || !DBuff->Height)
			goto ReleaseProcessLockSleep;

		//start frame (render)
		gRender.NewFrame(DBuff->Width, DBuff->Height, E(L"Calibri"), 17, 4);

		gRender.Line(0, 0, DBuff->Width, DBuff->Height, RGB(132, 145, 222), 5);

		gRender.Circle(100, 100, RGB(255, 0, 0), 100.f, 10);

		gRender.FillCircle(200, 200, RGB(0, 255, 0), 50.f);

		gRender.Rectangle(300, 300, 50, 50, RGB(255, 0, 0));

		gRender.RoundedRectangle(350, 350, 50, 50, RGB(255, 0, 0), 10.f);

		gRender.FillRectangle(400, 400, 50, 50, RGB(255, 0, 0));

		gRender.FillRoundedRectangle(450, 450, 50, 50, RGB(0, 0, 255), 10.f);

		gRender.String(500, 500, E(L"Memez"), 0, RGB(255,0,0));
		
		auto sz = gRender.TextRect(E(L"Memez"));
		gRender.Rectangle(500, 500, sz.cx, sz.cy, RGB(255, 0, 0));

		//end frame (copy)
		gRender.EndFrame(DBuff->Texture);
		DBuff->UpdateFrameCount();

		//unlock process & wait
		ReleaseProcessLockSleep:
		ImpCall(PsReleaseProcessExitSynchronization, TargetProc);
		Sleep(1);
	}

	//enable all apc
	ImpCall(KeLeaveGuardedRegion);

	//lol return 1!!
	return STATUS_NOT_IMPLEMENTED;
}

#pragma code_seg(push)
#pragma code_seg("INIT")

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
	xKdEnumerateDebuggingDevicesPtr = (PVOID*)RVA((ULONG64)EPtr(KeQueryAuxiliaryCounterFrequencyFn) + 4, 7);
	xKdEnumerateDebuggingDevicesVal = _InterlockedExchangePointer(xKdEnumerateDebuggingDevicesPtr, ThreadStartAddr);

	//create new thread
	CLIENT_ID Cid;
	ImpCall(RtlCreateUserThread, ZwCurrentProcess(), nullptr, false, 0, 0, 0, CallBack, Var, &Thread, &Cid);

	if (Thread)
	{
		//close useless handle
		ImpCall(ZwClose, Thread);

		//spoof thread start address
		PETHREAD EThread;
		ImpCall(PsLookupThreadByThreadId, Cid.UniqueThread, &EThread);
		auto StartAddrOff = *(USHORT*)(FindPatternSect(KBase, E("PAGE"), E("48 89 86 ? ? ? ? 48 8B 8C")) + 3);
		*(PVOID*)((ULONG64)EThread + StartAddrOff/*Win32StartAddress*/) = FakeStartAddr;
		ImpCall(ObfDereferenceObject, EThread);

		//wait exec kernel callback
		while (xKdEnumerateDebuggingDevicesPtr && xKdEnumerateDebuggingDevicesVal) {
			Sleep(10);
		}
	}

	//cleanup
	//UFree(Var); //hz
	DetachFromProcess(Process);

	//ret create status
	return (bool)Thread;
}

//driver entry point
NTSTATUS DriverEntry(PVOID a1, PVOID KBase)
{
	//import set
	ImpSet(ExAllocatePoolWithTag);
	ImpSet(ExFreePoolWithTag);
	ImpSet(IoGetCurrentProcess);
	ImpSet(KeAttachProcess);
	ImpSet(KeDelayExecutionThread);
	ImpSet(KeDetachProcess);
	ImpSet(KeEnterGuardedRegion);
	ImpSet(KeLeaveGuardedRegion);
	ImpSet(KeQueryAuxiliaryCounterFrequency);
	ImpSet(KeUserModeCallback);
	ImpSet(MmIsAddressValid);
	ImpSet(ObfDereferenceObject);
	ImpSet(PsAcquireProcessExitSynchronization);
	ImpSet(PsGetProcessPeb);
	ImpSet(PsLookupProcessByProcessId);
	ImpSet(PsLookupThreadByThreadId);
	ImpSet(PsReleaseProcessExitSynchronization);
	ImpSet(RtlCreateUserThread);
	ImpSet(ZwAllocateVirtualMemory);
	ImpSet(ZwClose);
	ImpSet(ZwFreeVirtualMemory);
	ImpSet(ZwQuerySystemInformation);
	ImpSet(ZwQueryVirtualMemory); 
	ImpSet(MmSecureVirtualMemory);

	//disable apc
	ImpCall(KeEnterGuardedRegion);

	//create kernel usermode thread
	SetupKernelThread(KBase, FakeThread);

	//enable all apc
	ImpCall(KeLeaveGuardedRegion);

	return STATUS_SUCCESS;
}

#pragma code_seg(pop)
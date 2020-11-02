//DBG Help
#ifdef DBG
#define wsp(a) DbgPrintEx(0, 0, "\nFACE WSTR: %ws\n", (a))
#define hp(a) DbgPrintEx(0, 0, "\nFACE HEX: 0x%p\n", (a))
#define sp(a) DbgPrintEx(0, 0, "\nFACE STR: %s\n", (a))
#define dp(a) DbgPrintEx(0, 0, "\nFACE DEC: %d\n", (a))
#endif

template <typename Type>
_FI Type EPtr(Type Ptr) {
	auto Key = (ULONG64)SharedUserData->Cookie *
		SharedUserData->Cookie *
		SharedUserData->Cookie *
		SharedUserData->Cookie;
	return (Type)((ULONG64)Ptr ^ Key);
}

_FI PEPROCESS AttachToProcess(HANDLE PID)
{
	//get eprocess
	PEPROCESS Process = nullptr;
	if (ImpCall(PsLookupProcessByProcessId, PID, &Process) || !Process)
		return nullptr;

	//take process lock
	if (ImpCall(PsAcquireProcessExitSynchronization, Process))
	{
		//process lock failed
		ImpCall(ObfDereferenceObject, Process);
		return nullptr;
	}

	//attach to process
	ImpCall(KeAttachProcess, Process);
	return Process;
}

_FI void DetachFromProcess(PEPROCESS Process)
{
	//check valid process
	if (Process != nullptr)
	{
		//de-attach to process
		ImpCall(KeDetachProcess);

		//cleanup & process unlock
		ImpCall(ObfDereferenceObject, Process);
		ImpCall(PsReleaseProcessExitSynchronization, Process);
	}
}

_FI void Sleep(LONG64 MSec) {
	LARGE_INTEGER Delay; Delay.QuadPart = -MSec * 10000;
	ImpCall(KeDelayExecutionThread, KernelMode, false, &Delay);
}

PVOID FindSection(PVOID ModBase, const char* Name, PULONG SectSize)
{
	//get & enum sections
	PIMAGE_NT_HEADERS NT_Header = NT_HEADER(ModBase);
	PIMAGE_SECTION_HEADER Sect = IMAGE_FIRST_SECTION(NT_Header);
	for (PIMAGE_SECTION_HEADER pSect = Sect; pSect < Sect + NT_Header->FileHeader.NumberOfSections; pSect++)
	{
		//copy section name
		char SectName[9]; SectName[8] = 0;
		*(ULONG64*)&SectName[0] = *(ULONG64*)&pSect->Name[0];

		//check name
		if (StrICmp(Name, SectName, true))
		{
			//save size
			if (SectSize) {
				ULONG SSize = SizeAlign(max(pSect->Misc.VirtualSize, pSect->SizeOfRawData));
				*SectSize = SSize;
			}

			//ret full sect ptr
			return (PVOID)((ULONG64)ModBase + pSect->VirtualAddress);
		}
	}

	//no section
	return nullptr;
}

PUCHAR FindPatternSect(PVOID ModBase, const char* SectName, const char* Pattern)
{
	//find pattern utils
	#define InRange(x, a, b) (x >= a && x <= b) 
	#define GetBits(x) (InRange(x, '0', '9') ? (x - '0') : ((x - 'A') + 0xA))
	#define GetByte(x) ((UCHAR)(GetBits(x[0]) << 4 | GetBits(x[1])))

	//get sect range
	ULONG SectSize;
	PUCHAR ModuleStart = (PUCHAR)FindSection(ModBase, SectName, &SectSize);
	PUCHAR ModuleEnd = ModuleStart + SectSize;

	//scan pattern main
	PUCHAR FirstMatch = nullptr;
	const char* CurPatt = Pattern;
	for (; ModuleStart < ModuleEnd; ++ModuleStart)
	{
		bool SkipByte = (*CurPatt == '\?');
		if (SkipByte || *ModuleStart == GetByte(CurPatt)) {
			if (!FirstMatch) FirstMatch = ModuleStart;
			SkipByte ? CurPatt += 2 : CurPatt += 3;
			if (CurPatt[-1] == 0) return FirstMatch;
		}

		else if (FirstMatch) {
			ModuleStart = FirstMatch;
			FirstMatch = nullptr;
			CurPatt = Pattern;
		}
	}

	//failed
	return nullptr;
}

_FI PVOID KAlloc(ULONG Size) {
	PVOID Buff = ImpCall(ExAllocatePoolWithTag, NonPagedPoolNx, Size, 'KgxD');
	MemZero(Buff, Size);
	return Buff;
}

_FI void KFree(PVOID Ptr) {
	ImpCall(ExFreePoolWithTag, Ptr, 'KgxD');
}

PVOID GetUserModuleBase(PEPROCESS Process, const char* ModName, bool LoadDll = false)
{
	//get peb & ldr
	PPEB PEB = ImpCall(PsGetProcessPeb, Process);
	if (!PEB || !PEB->Ldr) return nullptr;

	//process modules list (with peb->ldr)
	for (PLIST_ENTRY pListEntry = PEB->Ldr->InLoadOrderModuleList.Flink;
					 pListEntry != &PEB->Ldr->InLoadOrderModuleList;
					 pListEntry = pListEntry->Flink)
	{
		PLDR_DATA_TABLE_ENTRY pEntry = CONTAINING_RECORD(pListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
		if (StrICmp(ModName, pEntry->BaseDllName.Buffer, false))
			return pEntry->DllBase;
	}

	//no module
	return nullptr;
}

PVOID NQSI(SYSTEM_INFORMATION_CLASS Class)
{
	//get alloc size
	NewTry: ULONG ReqSize = 0;
	ImpCall(ZwQuerySystemInformation, Class, nullptr, ReqSize, &ReqSize);
	if (!ReqSize) goto NewTry;

	//call QuerySystemInfo
	PVOID pInfo = KAlloc(ReqSize);
	if (!NT_SUCCESS(ImpCall(ZwQuerySystemInformation, Class, pInfo, ReqSize, &ReqSize))) {
		KFree(pInfo); goto NewTry;
	}

	//ret buff
	return pInfo;
}

void CallUserMode(PVOID Func)
{
	//get user32 (KernelCallbackTable table ptr)
	PEPROCESS Process = ImpCall(IoGetCurrentProcess);
	PVOID ModBase = GetUserModuleBase(Process, E("user32"));
	PVOID DataSect = FindSection(ModBase, E(".data"), nullptr);
	ULONG64 AllocPtr = ((ULONG64)DataSect + 0x2000 - 0x8);
    ULONG64 CallBackPtr = (ULONG64)ImpCall(PsGetProcessPeb, Process)->KernelCallbackTable;
	ULONG Index = (ULONG)((AllocPtr - CallBackPtr) / 8);

	//store func ptr in place
	auto OldData = _InterlockedExchangePointer((PVOID*)AllocPtr, Func);

	//enable apc (FIX BSOD)
	ImpCall(KeLeaveGuardedRegion);

	//call usermode
	union Garbage { ULONG ulong; PVOID pvoid; } Garbage;
	ImpCall(KeUserModeCallback, Index, nullptr, 0, &Garbage.pvoid, &Garbage.ulong);
	
	//store old ptr in place
	_InterlockedExchangePointer((PVOID*)AllocPtr, OldData);

	//disable apc
	ImpCall(KeEnterGuardedRegion);
}

_FI PVOID UAlloc(ULONG Size, ULONG Protect = PAGE_READWRITE) {
	PVOID AllocBase = nullptr; SIZE_T SizeUL = SizeAlign(Size);
	ImpCall(ZwAllocateVirtualMemory, ZwCurrentProcess(), &AllocBase, 0, &SizeUL, MEM_COMMIT, Protect);
	return AllocBase;
}

_FI void UFree(PVOID Ptr) {
	SIZE_T SizeUL = 0;
	ImpCall(ZwFreeVirtualMemory, ZwCurrentProcess(), &Ptr, &SizeUL, MEM_RELEASE);
}

PEPROCESS GetProcessWModule(const char* ProcName, const char* ModName, PVOID* WaitModBase)
{
	//get process list
	PEPROCESS EProc = nullptr;
	PSYSTEM_PROCESS_INFO pInfo = (PSYSTEM_PROCESS_INFO)NQSI(SystemProcessInformation), pInfoCur = pInfo;

	while (true)
	{
		//get process name
		const wchar_t* ProcessName = pInfoCur->ImageName.Buffer;
		if (ImpCall(MmIsAddressValid, (PVOID)ProcessName))
		{
			//check process name
			if (StrICmp(ProcName, ProcessName, true))
			{
				//attach to process
				PEPROCESS Process = AttachToProcess(pInfoCur->UniqueProcessId);
				if (Process != nullptr)
				{
					//check wait module
					PVOID ModBase = GetUserModuleBase(Process, ModName);
					if (ModBase)
					{
						//save modbase
						if (WaitModBase)
							*WaitModBase = ModBase;

						//save eprocess
						EProc = Process;
						break;
					}

					//failed, no wait module
					DetachFromProcess(Process);
				}
			}
		}

		//goto next process entry
		if (!pInfoCur->NextEntryOffset) break;
		pInfoCur = (PSYSTEM_PROCESS_INFO)((ULONG64)pInfoCur + pInfoCur->NextEntryOffset);
	}

	//cleanup
	KFree(pInfo);
	return EProc;
}

PVOID GetProcAdress(PVOID ModBase, const char* Name)
{
	//parse headers
	PIMAGE_NT_HEADERS NT_Head = NT_HEADER(ModBase);
	PIMAGE_EXPORT_DIRECTORY ExportDir = (PIMAGE_EXPORT_DIRECTORY)((ULONG64)ModBase + NT_Head->OptionalHeader.DataDirectory[0].VirtualAddress);

	//process records
	for (ULONG i = 0; i < ExportDir->NumberOfNames; i++)
	{
		//get ordinal & name
		USHORT Ordinal = ((USHORT*)((ULONG64)ModBase + ExportDir->AddressOfNameOrdinals))[i];
		const char* ExpName = (const char*)ModBase + ((ULONG*)((ULONG64)ModBase + ExportDir->AddressOfNames))[i];

		//check export name
		if (StrICmp(Name, ExpName, true))
			return (PVOID)((ULONG64)ModBase + ((ULONG*)((ULONG64)ModBase + ExportDir->AddressOfFunctions))[Ordinal]);
	}

	//no export
	return nullptr;
}

PVOID GetKernelModuleBase(const char* ModName)
{
	PSYSTEM_MODULE_INFORMATION ModuleList = (PSYSTEM_MODULE_INFORMATION)NQSI(SystemModuleInformation);

	PVOID ModuleBase = 0;
	for (ULONG64 i = 0; i < ModuleList->ulModuleCount; i++)
	{
		SYSTEM_MODULE Module = ModuleList->Modules[i];
		if (StrICmp(&Module.ImageName[Module.ModuleNameOffset], ModName, true)) {
			ModuleBase = Module.Base;
			break;
		}
	}

	KFree(ModuleList);
	return ModuleBase;
}

template<typename Ret = void, typename... ArgT>
_FI Ret CallPtr(PVOID Fn, ArgT... Args) {
	typedef Ret(*ShellFn)(ArgT...);
	return ((ShellFn)Fn)(Args...);
}
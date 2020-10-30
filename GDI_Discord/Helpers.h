PVOID GetUserModuleBase(PEPROCESS Process, const char* ModName, bool LoadDll = false)
{
	//get peb & ldr
	PPEB PEB = PsGetProcessPeb(Process);
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

void CallUserMode(PVOID Func)
{
	//get user32 (KernelCallbackTable table ptr)
	PEPROCESS Process = IoGetCurrentProcess();
	PVOID ModBase = GetUserModuleBase(Process, "user32.dll");
	PVOID DataSect = FindSection(ModBase, ".data", nullptr);
	ULONG64 AllocPtr = ((ULONG64)DataSect + 0x2000 - 0x8);
    ULONG64 CallBackPtr = (ULONG64)PsGetProcessPeb(Process)->KernelCallbackTable;
	ULONG Index = (ULONG)((AllocPtr - CallBackPtr) / 8);

	//store func ptr in place
	auto OldData = _InterlockedExchangePointer((PVOID*)AllocPtr, Func);

	//enable apc (FIX BSOD)
	KeLeaveGuardedRegion();

	//call usermode
	union Garbage { ULONG ulong; PVOID pvoid; } Garbage;
	KeUserModeCallback(Index, nullptr, 0, &Garbage.pvoid, &Garbage.ulong);
	
	//store old ptr in place
	_InterlockedExchangePointer((PVOID*)AllocPtr, OldData);

	//disable apc
	KeEnterGuardedRegion();
}

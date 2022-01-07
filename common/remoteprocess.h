#pragma once
#include "kernel.h"
#include "shellcode.h"

class RemoteProcess {
private:
	const std::shared_ptr<const Kernel> pKernel;
	const HANDLE hProcess;
	const DWORD dwPid;
	void* RemoteEntryPoint;
public:
	bool VirtualProtectWrapper(void* pRemoteAddress, size_t Size, auto f) const {
		DWORD dwOldProtect;
		if (!RemoteVirtualProtect(pRemoteAddress, Size, PAGE_EXECUTE_READWRITE, &dwOldProtect))
			return false;

		f();

		if (!RemoteVirtualProtect(pRemoteAddress, Size, dwOldProtect, &dwOldProtect))
			return false;
		return true;
	}

	bool WriteProcessMemoryWrapper(void* pRemoteAddress, const void* pBuffer, size_t Size, auto f) const {
		std::vector<uint8_t> OriginalBytes(Size);
		if (!RemoteReadProcessMemory(pRemoteAddress, OriginalBytes.data(), Size, 0))
			return false;

		if (!VirtualProtectWrapper(pRemoteAddress, Size, [&] {
			if (!RemoteWriteProcessMemory(pRemoteAddress, pBuffer, Size, 0))
				return false;
			f();
			if (!RemoteWriteProcessMemory(pRemoteAddress, OriginalBytes.data(), Size, 0))
				return false;
			return true;
			})) return false;

		return true;
	}

	bool CreateRemoteThreadWrapper(void* pStartAddress, void* pParam, bool bWaitUntilExit, auto f) const {
		HANDLE hThread = RemoteCreateRemoteThread(0, 0, (LPTHREAD_START_ROUTINE)pStartAddress, pParam, 0, 0);
		if (!hThread)
			return 0;

		bool bSuccess = true;
		if (bWaitUntilExit)
			bSuccess = (WaitForSingleObject(hThread, 2000) == WAIT_OBJECT_0);
		else Sleep(1);

		if (bSuccess) f();
		else TerminateThread(hThread, 0);
		CloseHandle(hThread);
		return bSuccess;
	}

	BOOL RemoteVirtualProtect(LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect, PDWORD lpflOldProtect) const {
		return VirtualProtectEx(hProcess, lpAddress, dwSize, flNewProtect, lpflOldProtect);
	}

	LPVOID RemoteVirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect) const {
		return VirtualAllocEx(hProcess, lpAddress, dwSize, flAllocationType, flProtect);
	}

	BOOL RemoteVirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType) const {
		return VirtualFreeEx(hProcess, lpAddress, dwSize, dwFreeType);
	}

	BOOL RemoteWriteProcessMemory(LPVOID lpBaseAddress, LPCVOID lpBuffer, SIZE_T nSize, SIZE_T* lpNumberOfBytesWritten) const {
		return WriteProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesWritten);
	}

	BOOL RemoteReadProcessMemory(LPCVOID lpBaseAddress, LPVOID lpBuffer, SIZE_T nSize, SIZE_T* lpNumberOfBytesRead) const {
		return ReadProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesRead);
	}

	SIZE_T RemoteVirtualQuery(LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength) const {
		return VirtualQueryEx(hProcess, lpAddress, lpBuffer, dwLength);
	}

	HANDLE RemoteCreateRemoteThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress,
		LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId) const {
		return CreateRemoteThread(hProcess, lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags, lpThreadId);
	}

	BOOL RemoteTerminateProcess(UINT uExitCode) const {
		return TerminateProcess(hProcess, uExitCode);
	}

	RemoteProcess(DWORD dwPid, const std::shared_ptr<const Kernel> pKernel) :
		hProcess(OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPid)), pKernel(pKernel), dwPid(dwPid) {
		bool bSuccess = [&] {
			if (!hProcess)
				return false;
			HMODULE hModule;
			DWORD dwSize;
			if (!EnumProcessModules(hProcess, &hModule, sizeof(hModule), &dwSize))
				return false;
			MODULEINFO ModuleInfo;
			if (!GetModuleInformation(hProcess, hModule, &ModuleInfo, sizeof(ModuleInfo)))
				return false;
			RemoteEntryPoint = ModuleInfo.EntryPoint;
			return true;
		}();
		verify(bSuccess);
	}

	~RemoteProcess() {
		CloseHandle(hProcess);
	}

	bool RemoveNXBit(void* pRemoteAddress, size_t Size) const {
		if (!pKernel)
			return false;

		DWORD dwOldProtect;
		if (!RemoteVirtualProtect(pRemoteAddress, Size, PAGE_EXECUTE_READWRITE, &dwOldProtect) ||
			!RemoteVirtualProtect(pRemoteAddress, Size, PAGE_READWRITE, &dwOldProtect)) {
			return false;
		}

		CR3 cr3 = pKernel->GetDirectoryTableBase(dwPid);
		if (!cr3.Value)
			return false;

		for (size_t i = 0; i < Size; i += 0x1000) {
			uintptr_t VirtualAddress = (uintptr_t)pRemoteAddress + i;

			uint8_t byte;
			if (!RemoteReadProcessMemory((void*)VirtualAddress, &byte, sizeof(byte), 0))
				return false;

			PhysicalAddress PTEAddress = pKernel->dbvm.GetPTEAddress(VirtualAddress, cr3);
			if (!PTEAddress)
				return false;

			PTE pte;
			if (!pKernel->dbvm.ReadPhysicalMemory(PTEAddress, &pte, sizeof(pte)))
				return false;
			pte.ExecuteDisable = 0;
			if (!pKernel->dbvm.WritePhysicalMemory(PTEAddress, &pte, sizeof(pte)))
				return false;
		}

		return true;
	}

	bool BypassCFG() const {
		HMODULE hKernel32 = GetModuleHandleA("kernel32.dll"e);
		if (!hKernel32)
			return false;

		PIMAGE_DOS_HEADER dosHd = (PIMAGE_DOS_HEADER)hKernel32;
		PIMAGE_NT_HEADERS ntHd = (PIMAGE_NT_HEADERS)((uintptr_t)hKernel32 + dosHd->e_lfanew);

		if (!ntHd->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size)
			return true;

		PIMAGE_LOAD_CONFIG_DIRECTORY loadconfig = (PIMAGE_LOAD_CONFIG_DIRECTORY)((uintptr_t)hKernel32 +
			ntHd->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress);

		void* pFunc = 0;
		ShellCode_Ret shellcode_ret;
		RemoteReadProcessMemory((void*)loadconfig->GuardCFCheckFunctionPointer, &pFunc, sizeof(void*), 0);
		if (pFunc && !RemoteWriteProcessMemory(pFunc, &shellcode_ret, sizeof(shellcode_ret), 0))
			return false;

		pFunc = 0;
		ShellCode_JmpRax shellcode_jmprax;
		RemoteReadProcessMemory((void*)loadconfig->GuardCFDispatchFunctionPointer, &pFunc, sizeof(void*), 0);
		if (pFunc && !RemoteWriteProcessMemory(pFunc, &shellcode_jmprax, sizeof(shellcode_jmprax), 0))
			return false;

		return true;
	}

	bool IsPlatformMatched() const {
		BOOL bWow64 = TRUE;
		IsWow64Process(hProcess, &bWow64);

		SYSTEM_INFO SystemInfo;
		GetNativeSystemInfo(&SystemInfo);

#ifdef _WIN64
		return SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 && bWow64 == FALSE;
#else
		if (SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
			return TRUE;

		return SystemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 && bWow64 == TRUE;
#endif
	}

	bool VirtualAllocWrapper(const void* pBuffer, size_t Size, auto f) const {
		void* pAllocBase = RemoteVirtualAlloc(0, Size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!pAllocBase)
			return false;

		if (!RemoteWriteProcessMemory(pAllocBase, pBuffer, Size, 0)) {
			RemoteVirtualFree(pAllocBase, 0, MEM_RELEASE);
			return false;
		}

		f((const void*)pAllocBase);
		return RemoteVirtualFree(pAllocBase, 0, MEM_RELEASE);
	}

	template<class R, class A1, class A2, class A3, class A4>
	bool RemoteCall(void* pFunc, R pResult, A1 a1, A2 a2, A3 a3, A4 a4, bool bWaitUntilExit) const {
		bool bResult1 = false;
		bool bResult2 = false;
		bool bResult3 = false;
		bool bResult4 = false;

		ShellCode_RemoteCall lilyinfo(pFunc, a1, a2, a3, a4);
		bResult1 = VirtualAllocWrapper(&lilyinfo, sizeof(lilyinfo), [&](const void* pShellCode) {
			ShellCode_SetRaxAndCall ShellCode(pShellCode);
			bResult2 = WriteProcessMemoryWrapper(RemoteEntryPoint, &ShellCode, sizeof(ShellCode), [&] {
				bResult3 = CreateRemoteThreadWrapper(RemoteEntryPoint, 0, bWaitUntilExit, [&] {
					if (!RemoteReadProcessMemory(pShellCode, &lilyinfo, sizeof(lilyinfo), 0))
						return;
					if constexpr (std::is_pointer_v<R>) if (pResult)
						*pResult = (std::remove_pointer_t<R>)lilyinfo.GetReturnValue();
					bResult4 = true;
					});
				});
			});
		return bResult1 && bResult2 && bResult3 && bResult4;
	}
};
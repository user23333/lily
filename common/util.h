#pragma once
#include "harderror.h"
#include "encrypt_string.h"
static uintptr_t GetProcAddressVerified(const char* szModuleName, const char* szProcName);
const inline tNtRaiseHardError NtRaiseHardError = (tNtRaiseHardError)GetProcAddressVerified("ntdll.dll"e, "NtRaiseHardError"e);

#include <windows.h>
#include <Psapi.h>
#include <stdio.h>
#include <atlconv.h>
#include <algorithm>
#include <sstream>

#ifdef _WINDLL
//#define DPRINT
#else
#define DPRINT
#endif

#ifdef DPRINT
#define dprintf(...) [&]{ printf(__VA_ARGS__); putchar('\n'); }()
#else
#define dprintf(...) []{}()
#endif

static void MessageBoxCSRSS(const char* szText, const char* szCaption, UINT uType);

static void error(const char* szMsg, const char* szTitle = "Error!"e) {
#ifdef DEBUG
	MessageBoxA(0, szMsg, szTitle, MB_ICONERROR | MB_TOPMOST);
#else
	MessageBoxCSRSS(szMsg, szTitle, MB_ICONERROR);
#endif
	exit(0);
}

template<fixstr::basic_fixed_string path>
consteval auto ExtractOnlyFileName() {
	return path.substr<path.find_last_of('\\') + 1>();
}

#define STRING(x) #x
#define PACK(x) STRING(x)

#define verify(expression) \
[&]{ \
	if (expression) return; \
	error(EncryptedString<ExtractOnlyFileName<__FILE__ " : " PACK(__LINE__) "\n" #expression>()>(), "Assertion failed!"e); \
}()

static int CreateProcessCMD(const char* szPath) {
	char szCommand[0x100];
	sprintf(szCommand, "if exist \"%s\" start \"\" \"%s\""e, szPath, szPath);
	return system(szCommand);
}

static bool IsKeyPushing(int vKey) { return (GetAsyncKeyState(vKey) & 0x8000) != 0; }
static bool IsKeyPushed(int vKey) { return (GetAsyncKeyState(vKey) & 0x1) != 0; }

static std::wstring s2ws(const std::string& str) {
	USES_CONVERSION;
	return std::wstring(A2W(str.c_str()));
}

static std::string ws2s(const std::wstring& wstr) {
	USES_CONVERSION;
	return std::string(W2A(wstr.c_str()));
}

static std::wstring to_hex_string(uintptr_t i) {
	std::wstringstream s;
	s << L"0x" << std::hex << i;
	return s.str();
}

static std::wstring trim(const std::wstring& s) {
	auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c) {return std::isspace(c); });
	auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c) {return std::isspace(c); }).base();
	return (wsback <= wsfront ? std::wstring() : std::wstring(wsfront, wsback));
}

static uint64_t GetTickCountInMicroSeconds() {
	LARGE_INTEGER PerformanceCount, Frequency;
	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&PerformanceCount);
	return PerformanceCount.QuadPart * 1000000 / Frequency.QuadPart;
}

static void* VirtualAllocVerified(size_t Size, DWORD dwProtect) {
	void* const pResult = [&]() -> void* {
		void* const pMemory = VirtualAlloc(0, Size, MEM_COMMIT | MEM_RESERVE, dwProtect);
		if (!pMemory)
			return 0;

		for (size_t i = 0; i < Size; i += 0x1000) {
			uint8_t byte;
			if (!ReadProcessMemory((HANDLE)-1, (const void*)((uintptr_t)pMemory + i), &byte, 1, 0))
				return 0;
		}

		return pMemory;
	}();

	if (!pResult)
		error("VirtualAlloc failed"e);

	return pResult;
}

static HMODULE GetKernelModuleAddressVerified(const char* szModule) {
	const HMODULE hKernelModule = [&]()-> HMODULE {
		void* Drivers[0x100];
		DWORD cbNeeded;
		if (!EnumDeviceDrivers(Drivers, sizeof(Drivers), &cbNeeded))
			return 0;

		for (auto Driver : Drivers) {
			char szBaseName[MAX_PATH];
			if (!GetDeviceDriverBaseNameA(Driver, szBaseName, sizeof(szBaseName)))
				continue;
			if (_stricmp(szBaseName, szModule) == 0)
				return (HMODULE)Driver;
		}

		return 0;
	}();

	if (!hKernelModule)
		error(szModule, "Kernel module not exist."e);

	return hKernelModule;
}

static uintptr_t GetKernelProcAddressVerified(const char* szModuleName, const char* szProcName) {
	const uintptr_t Result = [&]() -> uintptr_t {
		const HMODULE hModule = LoadLibraryA(szModuleName);
		if (!hModule)
			return 0;

		const HMODULE hModuleKernel = GetKernelModuleAddressVerified(szModuleName);
		if (!hModuleKernel)
			return 0;

		const FARPROC ProcAddr = GetProcAddress(hModule, szProcName);
		FreeLibrary(hModule);

		if (!ProcAddr)
			return 0;

		return (uintptr_t)ProcAddr + (uintptr_t)hModuleKernel - (uintptr_t)hModule;
	}();

	if(!Result)
		error(szModuleName, szProcName);

	return Result;
}

static uintptr_t GetProcAddressVerified(const char* szModuleName, const char* szProcName) {
	const uintptr_t Result = [&]() -> uintptr_t {
		const HMODULE hModule = LoadLibraryA(szModuleName);
		if (!hModule)
			return 0;

		const FARPROC ProcAddr = GetProcAddress(hModule, szProcName);
		FreeLibrary(hModule);

		if (!ProcAddr)
			return 0;
		
		return (uintptr_t)ProcAddr;
	}();

	if (!Result)
		error(szModuleName, szProcName);

	return Result;
}

static void MessageBoxCSRSS(const char* szText, const char* szCaption, UINT uType) {
	if (!NtRaiseHardError)
		return;

	const std::wstring wText(szText, szText + strlen(szText));
	const std::wstring wCaption(szCaption, szCaption + strlen(szCaption));
	const UNICODE_STRING uText = { (USHORT)(wText.size() * 2), (USHORT)(wText.size() * 2), (PWCH)wText.c_str() };
	const UNICODE_STRING uCaption = { (USHORT)(wCaption.size() * 2), (USHORT)(wCaption.size() * 2), (PWCH)wCaption.c_str() };
	HARDERROR_RESPONSE Response;

	uintptr_t Params[] = { (uintptr_t)&uText, (uintptr_t)&uCaption, (uintptr_t)uType };
	constexpr auto NumOfParams = sizeof(Params) / sizeof(*Params);

	constexpr auto STATUS_SERVICE_NOTIFICATION = 0x40000018;
	NtRaiseHardError(STATUS_SERVICE_NOTIFICATION, NumOfParams, (1 << 0) | (1 << 1), Params, OptionOkNoWait, &Response);
}
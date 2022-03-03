#pragma once
#include "common/function_ref.hpp"

struct FName;

class TNameEntryArray
{
private:
	static constexpr uintptr_t ADDRESS_GNAMES = 0x8A2C7D8;
	static constexpr uint32_t ElementsPerChunk = 0x3FE0;

	//NumElements can be increased when name added to list
	//Don't use this for check index range -> "if(ID.ComparisonIndex >= NumElements)"
	//DWORD64 NumElements;
	uintptr_t BasePtr;

	static constexpr size_t NAME_SIZE = 0x200;
	struct FNameEntry
	{
	public:
		char pad[0x10];
		union
		{
			char	AnsiName[NAME_SIZE];
			wchar_t	WideName[NAME_SIZE];
		};
	};
public:
	TNameEntryArray();
	//DWORD64 GetNumElements() const { return NumElements; }
	bool GetNameByID(FName ID, char* szBuf, size_t SizeMax) const;
	unsigned GetNameHashByID(FName ID) const;
	unsigned GetNameHashByObjectPtr(uintptr_t Ptr) const;
	void EnumNames(tl::function<bool(FName ID, const char* szName)> f) const;
	FName FindName(const char* szNameToFind) const;
	void DumpAllNames() const;
};
/*
	Author: y11en
	Data: 2020.4
*/

#include "one.h"
#include "sc.h"
#include "mypeb.h"

// ��������ʱ������ڴ���ƫ��
uint32_t get_rtoffset()
{
	uint32_t r = 0;

#ifndef _WIN64
	_asm
	{
		call _f;
	_f:
		pop eax;
		sub eax, offset _f;
		mov r, eax;
	}
#endif // 

	return r;
}

LONG_PTR get_kernel32()
{
	LONG_PTR ret;

#ifdef _WIN64
	ret = __readgsqword(0x60);
	ret = *(UINT_PTR*)(ret + 0x18);
	ret = *(UINT_PTR*)(ret + 0x30);
	ret = *(UINT_PTR*)ret;
	ret = *(UINT_PTR*)ret;
	ret = *(UINT_PTR*)(ret + 0x10);
#else
	ret = __readfsdword(0x30);
	ret = *(UINT_PTR*)(ret + 0x0C);
	ret = *(UINT_PTR*)(ret + 0x14);
	ret = *(UINT_PTR*)ret;
	ret = *(UINT_PTR*)ret;
	ret = *(UINT_PTR*)(ret + 0x10);
#endif // _WIN64

	return ret;
}


#ifndef _WIN64


#endif // !_WIN64

// BKDhash
// �����ַ���hash
uint32_t calc_hash(char* str)
{
	uint32_t seed = 131; // 31 131 1313 13131 131313 etc..
	uint32_t hash = 0;
	while (*str) {
		hash = hash * seed + (*str++);
	}
	return (hash & 0x7FFFFFFF);
}

uint32_t calc_hashW(wchar_t* wstr)
{
	uint32_t seed = 131; // 31 131 1313 13131 131313 etc..
	uint32_t hash = 0;
	while (*wstr)
	{
		hash = hash * seed + (*wstr++);
	}
	return (hash & 0x7FFFFFFF);
}

void* get_export_byhash(HMODULE module, uint32_t func_hash, T_GetProcAddress getproc)
{
	uint32_t* fn, * fa;
	uint16_t* ford;
	uint32_t i;
	uint32_t key;
	const char* name;

	PIMAGE_DOS_HEADER doshead = (PIMAGE_DOS_HEADER)module;
	PIMAGE_NT_HEADERS nthead = (PIMAGE_NT_HEADERS)((char*)module + doshead->e_lfanew);
	PIMAGE_DATA_DIRECTORY dataDict = &nthead->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (dataDict->VirtualAddress && dataDict->Size)
	{
		PIMAGE_EXPORT_DIRECTORY exportDir = (PIMAGE_EXPORT_DIRECTORY)
			(dataDict->VirtualAddress + (char*)module);

		if (exportDir->NumberOfNames)
		{
			fn = (uint32_t*)((char*)module + exportDir->AddressOfNames); // ��������
			fa = (uint32_t*)((char*)module + exportDir->AddressOfFunctions);
			ford = (uint16_t*)((char*)module + exportDir->AddressOfNameOrdinals);

			for (i = 0; i < exportDir->NumberOfNames; ++i)
			{
				name = fn[i] + (char*)module;
				key = calc_hash((char*)name);
				if (func_hash == key)
				{
					return getproc ? getproc(module, name) : (void*)(fa[ford[i]] + (char*)module);
				}
			}
		}
	}
	return 0;
}

// ����loadlibray�����Σ�ͨ�������ֱ�ӻ�ȡ������ַ
HMODULE get_import_module(DWORD hash, DWORD len)
{
	PPEB Peb;
	LIST_ENTRY* ListHead, * Current;
	PCLDR_DATA_TABLE_ENTRY pstEntry;

#ifdef _WIN64
	Peb = (PPEB)__readgsqword(0x60);
#else
	Peb = (PPEB)__readfsdword(0x30);
#endif

	// ���� VEH
	Peb->EnvironmentUpdateCount = 1;

	ListHead = &(Peb->Ldr->InLoadOrderModuleList);
	Current = ListHead->Flink;
	while (Current != ListHead)
	{

		pstEntry = CONTAINING_RECORD(Current, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
		
		if (len == pstEntry->BaseDllName.Length)
		{
			if (calc_hashW(pstEntry->BaseDllName.Buffer) == hash)
				return (HMODULE)pstEntry->DllBase;
		}
		Current = pstEntry->InLoadOrderLinks.Flink;
	}

	return 0;
}

typedef BOOL(__stdcall* pfnDllMain)(HMODULE, DWORD, LPVOID);
void* mem_loaddll(NativeApi* func, void* dll, void* name, void* param, void** addr)
{
	// ���pe��ʽ
	PIMAGE_NT_HEADERS nth;
	PIMAGE_DOS_HEADER dosh = (PIMAGE_DOS_HEADER)dll;
	PIMAGE_SECTION_HEADER section;
	PIMAGE_DATA_DIRECTORY data_dir;
	PIMAGE_BASE_RELOCATION reloc;
	PIMAGE_EXPORT_DIRECTORY expot;
	PIMAGE_IMPORT_DESCRIPTOR imptb;
	PIMAGE_IMPORT_BY_NAME ibn;
	PIMAGE_TLS_DIRECTORY tls;
	PIMAGE_TLS_CALLBACK* cb;

	HMODULE dll_base;
	PIMAGE_THUNK_DATA thunk, iat;


	uint16_t* reloc_data;
	uint32_t nrelec;

	uint32_t* target;
	uint64_t* target64;
	pfnDllMain dllmain;

	int i;
	void* base;

	void* src, * dst;
	const char* str = 0;

	uint32_t* fn, * fa;

	if (dll == 0) return 0;


	// PE��ʽ���

	if (dosh->e_magic != IMAGE_DOS_SIGNATURE) return 0;

	nth = (PIMAGE_NT_HEADERS)((char*)dll + dosh->e_lfanew);
	if (nth->Signature != IMAGE_NT_SIGNATURE) return 0;
#ifdef _WIN64
	if (nth->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
		nth->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
#else
	if (nth->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
		nth->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return 0;
#endif

	base = func->allocmem(0, nth->OptionalHeader.SizeOfImage, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (0 == base) return 0;


	// ���� dos �� nt ͷ
	func->movemem(base, dll, nth->OptionalHeader.SizeOfHeaders);	// dos + nt + section_head �ܴ�С
	section = (PIMAGE_SECTION_HEADER)((char*)nth + sizeof(IMAGE_NT_HEADERS));

	// ӳ���
	for (int i = 0; i < nth->FileHeader.NumberOfSections; i++)
	{
		if (section[i].VirtualAddress == 0) continue;

		src = (char*)dll + section[i].PointerToRawData;
		dst = (char*)base + section[i].VirtualAddress;

		if (section[i].SizeOfRawData != 0)
		{
			func->movemem(dst, src, section[i].SizeOfRawData);
		}
		else
		{
			if (nth->OptionalHeader.SectionAlignment > 0)
			{
				func->zeromem(dst, nth->OptionalHeader.SectionAlignment);
			}
		}
	}

	// ���ض�λ
	data_dir = &nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
	if (data_dir->VirtualAddress && data_dir->Size)
	{
		reloc = (PIMAGE_BASE_RELOCATION)((char*)base + data_dir->VirtualAddress);
		while (reloc->VirtualAddress + reloc->SizeOfBlock)	// һ��ҳ
		{
			reloc_data = (uint16_t*)((char*)reloc + sizeof(IMAGE_BASE_RELOCATION));
			nrelec = (reloc->SizeOfBlock - sizeof(*reloc)) / sizeof(uint16_t);

			for (i = 0; i < nrelec; ++i)		// ҳ����Ҫ�ض�λ�����ݵ�ָ��
			{
				if ((reloc_data[i] >> 12) == IMAGE_REL_BASED_HIGHLOW)
				{
#ifdef _WIN64
					target64 = (uint64_t*)((char*)base + reloc->VirtualAddress + (reloc_data[i] & 0x0FFF));
					*target64 += (uint64_t)base - (uint64_t)nth->OptionalHeader.ImageBase;

#else
					target = (uint32_t*)((char*)base + reloc->VirtualAddress + (reloc_data[i] & 0x0FFF)); // ҳ��ƫ��
					*target += (uint32_t)base - (uint32_t)nth->OptionalHeader.ImageBase;
#endif
				}
			}
			reloc = (PIMAGE_BASE_RELOCATION)((char*)reloc + reloc->SizeOfBlock); // �¸�ҳ
		}
	}

	// �޵����
	data_dir = &nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (data_dir->VirtualAddress && data_dir->Size)
	{
		imptb = (PIMAGE_IMPORT_DESCRIPTOR)((char*)base + data_dir->VirtualAddress);
		for (; imptb->Name; ++imptb)
		{
			str = (char*)base + imptb->Name;	// dll ����
			dll_base = func->loadlib(str);
			if (dll_base)
			{
				thunk = (PIMAGE_THUNK_DATA)((char*)base + (imptb->OriginalFirstThunk ? imptb->OriginalFirstThunk : imptb->FirstThunk));
				iat = (PIMAGE_THUNK_DATA)((char*)base + imptb->FirstThunk);

				for (; thunk->u1.AddressOfData; ++thunk, iat++)
				{
					// ��ŵ���
					if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal))
					{
						str = (char*)IMAGE_ORDINAL(thunk->u1.Ordinal); // ���
					}
					else // ���Ƶ���
					{
						ibn = (PIMAGE_IMPORT_BY_NAME)((char*)base + thunk->u1.AddressOfData);
						str = (char*)ibn->Name;
					}

					dst = func->getproc(dll_base, str);
#ifdef _WIN64
					iat->u1.Function = (uint64_t)dst;
#else
					iat->u1.Function = (uint32_t)dst;
#endif // _WIN64
				}
			}
			else
			{
				func->freemem(base, nth->OptionalHeader.SizeOfImage, MEM_DECOMMIT);
				return 0;
			}

		}
	}

	// ���� TLS
	data_dir = &nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
	if (data_dir->VirtualAddress)
	{
		tls = (PIMAGE_TLS_DIRECTORY)((char*)base + data_dir->VirtualAddress);
		cb = (PIMAGE_TLS_CALLBACK*)tls->AddressOfCallBacks;
		while (cb)
		{
			(*cb)(base, DLL_PROCESS_ATTACH, 0);
			++cb;
		}
	}

	// ������ڵ㺯��
	dllmain = (pfnDllMain)((char*)base + nth->OptionalHeader.AddressOfEntryPoint);
	if (dllmain)
	{
		dllmain((HMODULE)base, DLL_PROCESS_ATTACH, param);
	}

	// ���õ�������
	if (name)
	{
		*addr = get_export_byhash((HMODULE)base, (uint32_t)name, 0);
	}

	return 0;
}


char* LoadFile(const char* filePath, size_t* fileSize)
{
	FILE* f = 0;
	char* s;
	int n, t;

	fopen_s(&f, filePath, "rb");

	if (!f) {
		return 0;
	}

	if (fseek(f, 0, SEEK_END) < 0) {
		fclose(f);
		return 0;
	}

	n = ftell(f);
	if (n < 0) {
		fclose(f);
		return 0;
	}

	if (fseek(f, 0, SEEK_SET) < 0) {
		fclose(f);
		return 0;
	}

	s = (char*)malloc(n + 1);
	if (!s) {
		fclose(f);
		return 0;
	}

	t = fread(s, 1, n, f);
	if (t != n) {
		free(s);
		fclose(f);
		return 0;
	}

	if (fileSize)
		*fileSize = t;

	fclose(f);
	return s;
}
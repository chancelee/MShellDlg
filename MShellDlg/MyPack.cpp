#include "stdafx.h"
#include "MyPack.h"
#include "lz4.h"

MyPack::MyPack()
{
}


MyPack::~MyPack()
{
}
#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")
PIMAGE_DOS_HEADER MyPack::GetDosHeader(DWORD buff)
{
	return PIMAGE_DOS_HEADER(buff);
}

PIMAGE_NT_HEADERS MyPack::GetNTHeaders(DWORD buff)
{
	return PIMAGE_NT_HEADERS(GetDosHeader(buff)->e_lfanew + buff);
}

PIMAGE_FILE_HEADER MyPack::GetFileHeader(DWORD buff)
{
	return &GetNTHeaders(buff)->FileHeader;
}

PIMAGE_OPTIONAL_HEADER MyPack::GetOptHeader(DWORD buff)
{
	return &GetNTHeaders(buff)->OptionalHeader;
}
//��ȡ����ͷ
PIMAGE_SECTION_HEADER MyPack::GetSecHeader(DWORD buff)
{
	return IMAGE_FIRST_SECTION(GetNTHeaders(buff));
}
PIMAGE_SECTION_HEADER MyPack::GetSection(DWORD buff, LPCSTR SectionName)
{
	//1. ��ȡ���α�ĵ�һ��
	auto SectionTable = IMAGE_FIRST_SECTION(GetNTHeaders(buff));
	//2. ������������
	for (int i = 0; i < GetFileHeader(buff)->NumberOfSections; ++i)
	{
		if (!memcmp((PVOID)SectionName, SectionTable[i].Name, 8))
		{
			return &SectionTable[i];
		}
	}
	return nullptr;

}

DWORD MyPack::Alignment(DWORD n, DWORD align)
{
	return n % align == 0 ? n : (n / align + 1)*align;
}

//��ȡ�ض�λ���rva
DWORD MyPack::GetRelocRVA()
{
	return GetOptHeader(fileBuff)->DataDirectory[5].VirtualAddress;
}


BOOL MyPack::IsPE()
{
	if (GetDosHeader(fileBuff)->e_magic != IMAGE_DOS_SIGNATURE)
	{
		return FALSE;
	}
	if (GetNTHeaders(fileBuff)->Signature != IMAGE_NT_SIGNATURE)
	{
		return FALSE;
	}
	return TRUE;
}
//����PE�ļ���������
BOOL MyPack::LoadFile(LPCWSTR FileName)
{
	//1.����ļ����ھʹ��ļ�
	HANDLE hFile = CreateFile(FileName, GENERIC_READ, NULL,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		MessageBox(0, L"�ļ���ʧ��", L"����", 0);
		return FALSE;
	}
	//2.���ļ���С,��ʹ�������С���뻺����
	fileSize = GetFileSize(hFile, NULL);
	fileBuff = (DWORD)calloc(fileSize, sizeof(BYTE));
	//3.�����ڴ�
	DWORD Read = 0;
	ReadFile(hFile, (LPVOID)fileBuff, fileSize, &Read, NULL);
	//4.�ж��Ƿ���PE�ļ�
	if (IsPE() == FALSE)
	{
		MessageBox(0, L"����PE�ļ�", L"����", 0);
		CloseHandle(hFile);
		return FALSE;
	}
	//5.�رվ��
	CloseHandle(hFile);
	return TRUE;
}
//����stub���ڴ���
BOOL MyPack::LoadStub(LPCSTR dllName)
{
	//�Բ�ִ��DLLMain�ķ�ʽ����dllģ��
	DllBase = (DWORD)LoadLibraryExA(dllName,
		NULL, DONT_RESOLVE_DLL_REFERENCES);
	if (DllBase == NULL)
	{
		MessageBox(0, L"dll���ش���", L"����", 0);
		return FALSE;
	}
	//��dll�л�ȡstart����������������VA
	DWORD StartAdd = (DWORD)GetProcAddress((HMODULE)DllBase, "start");
	if (StartAdd == NULL)
	{
		MessageBox(0, L"��ȡstart������ַ����", L"����", 0);
		return FALSE;
	}
	startOffset = StartAdd - DllBase - GetSection(DllBase, ".text")->VirtualAddress;
	//��ȡ������Ϣ
	ShareData = (PSHAREDATA)GetProcAddress((HMODULE)DllBase, "ShareData");
	
	SetTLS = (DWORD)GetProcAddress((HMODULE)DllBase, "SetTls");

	ShareData->relocRVA = GetRelocRVA();//����ԭ�����ض�λ���RVA
	ShareData->BaseImage = GetOptHeader(fileBuff)->ImageBase;//����ԭ������ػ�ַ
	ShareData->ImportRVA = (DWORD)GetOptHeader(fileBuff)->DataDirectory[1].VirtualAddress;
}

int MyPack::RvaToFoa(DWORD Rva)
{
	PIMAGE_NT_HEADERS pNt = GetNTHeaders(fileBuff);
	PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNt);
	for (int i = 0; i < pNt->FileHeader.NumberOfSections; i++)
	{
		if (Rva >= pSection->VirtualAddress&&
			Rva <= pSection->VirtualAddress + pSection->Misc.VirtualSize)
		{
			// ����ļ���ַΪ0,���޷����ļ����ҵ���Ӧ������
			if (pSection->PointerToRawData == 0)
			{
				return -1;
			}
			return Rva - pSection->VirtualAddress + pSection->PointerToRawData;
		}
		pSection = pSection + 1;
	}
	return -1;
}

VOID MyPack::EncryIAT()
{
	GetOptHeader(fileBuff)->DataDirectory[1].VirtualAddress = 0;
	GetOptHeader(fileBuff)->DataDirectory[1].Size = 0;
						  
	GetOptHeader(fileBuff)->DataDirectory[12].VirtualAddress = 0;
	GetOptHeader(fileBuff)->DataDirectory[12].Size = 0;
}

VOID MyPack::SetOEP()
{
	ShareData->oldOep = GetOptHeader(fileBuff)->AddressOfEntryPoint;//����ɵ�OEP
	DWORD a = GetSection(fileBuff, ".pack")->VirtualAddress;
	GetOptHeader(fileBuff)->AddressOfEntryPoint = startOffset +
		GetSection(fileBuff, ".pack")->VirtualAddress;
}
struct TypeOffset
{
	WORD Offset : 12;
	WORD Type : 4;
};
//�޸��Ǵ�����ض�λ
VOID MyPack::FixDLLReloc()
{
	DWORD Size = 0, oldProtect = 0;
	//��ȡ�ض�λ��
	auto RelocTable = (PIMAGE_BASE_RELOCATION)
		ImageDirectoryEntryToData((PVOID)DllBase, TRUE, 5, &Size);
	//���SizeofTable��Ϊ�գ�˵�������ض�λ��
	while (RelocTable->SizeOfBlock)
	{
		//�޸ķ�������
		VirtualProtect((LPVOID)(DllBase + RelocTable->VirtualAddress),
			0x1000, PAGE_READWRITE, &oldProtect);
		//��ȡ�ض�λ������
		int relCount = (RelocTable->SizeOfBlock - 8) / 2;
		//����ض�λ��
		TypeOffset* pBlock = (TypeOffset *)(RelocTable + 1);
		for (int i = 0; i < relCount; ++i)
		{
			if (pBlock[i].Type == 3)
			{
				//��Ҫ�ض�λ��λ��
				DWORD* addr = (DWORD*)(DllBase + RelocTable->VirtualAddress
					+ pBlock[i].Offset);
				//����ƫ��
				DWORD itemOffset = *addr - DllBase - GetSection(DllBase, ".text")->VirtualAddress;
				//���ض�λ�������
				*addr = itemOffset + GetOptHeader(fileBuff)->ImageBase +
					GetSection(fileBuff, ".pack")->VirtualAddress;
			}
		}
		//�޸ķ�������
		VirtualProtect((LPVOID)(DllBase + RelocTable->VirtualAddress),
			0x1000, oldProtect, &oldProtect);
		//�ҵ���һ���ض�λ��
		RelocTable = (PIMAGE_BASE_RELOCATION)
			((DWORD)RelocTable + RelocTable->SizeOfBlock);

	}
	

}

VOID MyPack::CopySecData(LPCSTR desSec, LPCSTR srcSec)
{
	auto srcData = (PBYTE)(GetSection(DllBase, srcSec)->VirtualAddress + DllBase);
	auto desData = (PBYTE)(GetSection(fileBuff, desSec)->PointerToRawData + fileBuff);
	memcpy(desData, srcData, GetSection(DllBase, srcSec)->SizeOfRawData);
}

VOID MyPack::XOREncrySec(LPCSTR secName)
{
	//1. ���Ҫ�������ε���Ϣ
	auto enSec = GetSection(fileBuff, ".text");
	//2. ��ȡ�����ֶ����ڴ��е�λ��
	PBYTE enData = (PBYTE)(enSec->PointerToRawData + fileBuff);
	//3. ��д����ʱ��Ҫ�ṩ����Ϣ
	srand((unsigned int)time(0));
	ShareData->ekey = rand() % 0xff;
	ShareData->erva = enSec->VirtualAddress;
	ShareData->esize = enSec->SizeOfRawData;
	//4. ��ʼѭ������
	for (int i=0;i<ShareData->esize;++i)
	{
		enData[i] ^= ShareData->ekey;
	}
	
}
//��stub����������,�������µ�OEP
VOID MyPack::AddSection(LPCSTR desSec,LPCSTR srcSec)
{
	//1. ��ȡ�����α����һ��Ԫ�صĵ�ַ
	auto LastSection = IMAGE_FIRST_SECTION(GetNTHeaders(fileBuff)) +
		(GetFileHeader(fileBuff)->NumberOfSections - 1);
	//2. �ҵ���������ε�λ��
	auto NewSec = LastSection + 1;
	//3. ��������+1
	GetFileHeader(fileBuff)->NumberOfSections += 1;
	//4. ��dll���ҵ�Դ����
	auto srcSection = GetSection(DllBase, srcSec);
	//5. ��Դ���ο�������ͷ��Ϣ
	memcpy(NewSec, srcSection, sizeof(IMAGE_SECTION_HEADER));
	//6. ����������ͷ�е�����
	memcpy(NewSec->Name, desSec, 7);
	//7. ���������ε�RVA=��һ�����ε�RVA+������ڴ��С
	NewSec->VirtualAddress = LastSection->VirtualAddress +
		Alignment(LastSection->Misc.VirtualSize, GetOptHeader(fileBuff)->SectionAlignment);
	//8. ���������ε�FOA=��һ�����ε�FOA+������ļ���С
	NewSec->PointerToRawData = LastSection->PointerToRawData +
		Alignment(LastSection->SizeOfRawData, GetOptHeader(fileBuff)->FileAlignment);
	//10. �޸�SizeOfImage
	GetOptHeader(fileBuff)->SizeOfImage =
		NewSec->VirtualAddress + NewSec->Misc.VirtualSize;
	//���Ҫ��ӵ����������ض�λ���͸ı�ԭ������ض�λ��
	if (strcmp(srcSec, ".reloc") == 0)
	{
		GetOptHeader(fileBuff)->DataDirectory[5].VirtualAddress = NewSec->VirtualAddress;
		GetOptHeader(fileBuff)->DataDirectory[5].Size = NewSec->Misc.VirtualSize;
	}
	//9. ���¼����ļ���С�������µĿռ�
	fileSize = NewSec->SizeOfRawData + NewSec->PointerToRawData;
	fileBuff = (DWORD)realloc((VOID*)fileBuff, fileSize);
	
	

}
VOID MyPack::lz4Compress(const char* SectionName)
{
	PIMAGE_SECTION_HEADER ptext = GetSection(fileBuff, SectionName);
	PIMAGE_SECTION_HEADER ptextNext = ptext + 1;

	//1. ��ȡ�ֶ����ڴ��е�λ��
	auto textData = (char*)(ptext->PointerToRawData + fileBuff);

	// ����ѹ��ǰ��Ϣ
	// ѹ��ǰ��СSize
	ShareData->FrontCompSize = ptext->SizeOfRawData;


	// ---------------------------------��ʼѹ��
	// 1 ��ȡԤ����ѹ������ֽ���:
	int compress_size = LZ4_compressBound(ShareData->FrontCompSize);
	// 2. �����ڴ�ռ�, ���ڱ���ѹ���������
	char* pBuff = new char[compress_size];
	// 3. ��ʼѹ���ļ�����(��������ѹ����Ĵ�С)

	ShareData->LaterCompSize = LZ4_compress(
		(const char*)textData,/*ѹ��ǰ������*/
		pBuff, /*ѹ���������*/
		ptext->SizeOfRawData/*�ļ�ԭʼ��С*/);

	memcpy(textData, pBuff, ShareData->LaterCompSize);

	//�޸�����ͷ�������
	ptext->SizeOfRawData = Alignment(ShareData->LaterCompSize, 0x200);
	//3. ��һ���ε��ļ�ĩβ
	// û�к�һ�����Σ��Ͳ���Ҫ����
	while (ptextNext->VirtualAddress)
	{
		// ��ǰ���δ�С
		long DesSize = ptext->SizeOfRawData;
		// �ƶ���������κ���
		char * pDest = (char*)(ptext->PointerToRawData + fileBuff + DesSize);

		// �¸����δ�С
		long SrcSize = ptextNext->SizeOfRawData;
		// ��һ������λ��
		char * pSrc = (char*)(ptextNext->PointerToRawData + fileBuff);

		// ��������
		memcpy(pDest, pSrc, SrcSize);

		// �޸��¸�����λ�� ����FileBase��ӦΪ�������ڴ���
		ptextNext->PointerToRawData = ptext->PointerToRawData + DesSize;

		// ���������¸�����
		ptext += 1;
		ptextNext += 1;

	}
	// 7.�����޸��ļ�ʵ�ʴ�С
// ʵ�ʴ�С = ���һ������λ�� + ������δ�С
	fileSize = ptext->PointerToRawData + ptext->SizeOfRawData;

	// 8.�����޸��ļ���С
	fileBuff = (DWORD)realloc((VOID*)fileBuff, fileSize);

	// 9.�ͷſռ�
	delete[]pBuff;
}
//�����ļ� 
BOOL MyPack::SaveFile(LPCWSTR FileName)
{
	HANDLE hFile = CreateFile(FileName, GENERIC_WRITE, NULL,
		NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		MessageBox(0, L"�ļ���ʧ��", L"����", 0);
		return FALSE;

	}
	DWORD Write = 0;
	WriteFile(hFile, (LPVOID)fileBuff, fileSize, &Write, NULL);
	CloseHandle(hFile);
	return TRUE;
}
// �����ض�λ��
VOID MyPack::FixReloc()
{
	DWORD Size = 0, OldProtect = 0;

	// ��ȡ��������ض�λ��
	auto RealocTable = (PIMAGE_BASE_RELOCATION)
		ImageDirectoryEntryToData((PVOID)DllBase, TRUE, 5, &Size);

	// ��� SizeOfBlock ��Ϊ�գ���˵�������ض�λ��
	while (RealocTable->SizeOfBlock)
	{
		// �ض�λ��VirtualAddress �ֶν����޸ģ���Ҫ���ض�λ���ɿ�д
		VirtualProtect((LPVOID)(&RealocTable->VirtualAddress),
			0x4, PAGE_READWRITE, &OldProtect);

		// ����VirtualAddress���ӿ��е�text�� Ŀ�����pack��
		// �޸���ʽ ��VirtualAddress - ��.text.VirtualAddress  + Ŀ�����.pack.VirtualAddress
		RealocTable->VirtualAddress -= GetSection(DllBase, ".text")->VirtualAddress;
		RealocTable->VirtualAddress += GetSection(fileBuff, ".pack")->VirtualAddress;
		// ��ԭԭ���εĵı�������
		VirtualProtect((LPVOID)(&RealocTable->VirtualAddress),
			0x1000, OldProtect, &OldProtect);

		// �ҵ���һ���ض�λ��
		RealocTable = (PIMAGE_BASE_RELOCATION)
			((DWORD)RealocTable + RealocTable->SizeOfBlock);
	}

	return;
}

VOID MyPack::DealWithTLS() {
	//��ȡ��չͷ
	IMAGE_OPTIONAL_HEADER *pOptionHeader = GetOptHeader(fileBuff);
	DWORD dwImageBase = pOptionHeader->ImageBase;

	//�ж�TLS�Ƿ����
	if (pOptionHeader->DataDirectory[9].VirtualAddress == 0) {
		ShareData->bTlsEable = FALSE;
	}
 	else 
	{
		//�رճ�����ض�λ
		GetOptHeader(fileBuff)->DllCharacteristics = 0x8100;
		ShareData->bTlsEable = TRUE;
		PIMAGE_TLS_DIRECTORY32 g_lpTlsDir =
			(PIMAGE_TLS_DIRECTORY32)(RvaToFoa(pOptionHeader->DataDirectory[9].VirtualAddress) + fileBuff);
		ShareData->dwCallBackAddress = g_lpTlsDir->AddressOfCallBacks;
		DWORD dwOld = 0;
		DWORD a = g_lpTlsDir->AddressOfCallBacks - ShareData->BaseImage;//VA->RVA
		a = RvaToFoa(a);
		a += fileBuff;
		VirtualProtect((PVOID)a, 4, PAGE_READWRITE, &dwOld);
		a = SetTLS - DllBase -
			GetSection(DllBase,".text")->VirtualAddress + GetSection(fileBuff,".pack")->VirtualAddress
			+ ShareData->BaseImage;//0x426830; 
		VirtualProtect((PVOID)a, 4, dwOld, &dwOld);

	}
}

#pragma once
#include <windows.h>

typedef struct {
	//ԭʼOEP
	long oldOep = 0;
	//���ܵ�rva, ѹ����RVA
	long erva = 0;
	//���ܵĴ�С
	long esize = 0;
	//���ܵ�key
	unsigned char ekey = 0;
	//ԭ�����ض�λ���RVA
	DWORD relocRVA = 0;
	//ԭ�����Ĭ�ϼ��ػ�ַ
	DWORD BaseImage = 0;
	//����� 
	DWORD ImportRVA = 0; 
	// ѹ��ǰ��С
	DWORD FrontCompSize = 0;
	// ѹ����Ĵ�С
	DWORD LaterCompSize = 0;
	//�Ƿ���TLS
	BOOL bTlsEable = false;    
	//Tls�ص�������ַ
	DWORD dwCallBackAddress;   


}SHAREDATA, *PSHAREDATA;

class MyPack
{
public:
	MyPack();
	~MyPack();
private:
	DWORD fileBuff = 0;//�ļ�����Ŀռ�
	DWORD fileSize = 0;//�ļ���С
	DWORD startOffset = 0;//����start�����Ķ���ƫ�ƣ����Լ�����OEP
	PSHAREDATA ShareData = nullptr; // ���湲�����ݿ飬��Ҫ�����ṩ��Ϣ���Ǵ���
	DWORD SetTLS = 0; // ���湲�����ݿ飬��Ҫ�����ṩ��Ϣ���Ǵ���
	DWORD DllBase = 0;//dll�ļ��ػ�ַ

private:
	//���ߺ��������ڶ�ȡPE�ļ���Ϣ
	PIMAGE_DOS_HEADER GetDosHeader(DWORD buff);
	PIMAGE_NT_HEADERS GetNTHeaders(DWORD buff);
	PIMAGE_FILE_HEADER GetFileHeader(DWORD buff);
	PIMAGE_OPTIONAL_HEADER GetOptHeader(DWORD buff);
	BOOL IsPE();
	PIMAGE_SECTION_HEADER GetSecHeader(DWORD buff);
	PIMAGE_SECTION_HEADER GetSection(DWORD buff, LPCSTR SectionName);
	DWORD Alignment(DWORD n, DWORD align);//���ڰ���ָ���ֽڶ���
	DWORD GetRelocRVA();

public:
	//1.��ȡPE�ļ����ڴ�
	BOOL LoadFile(LPCWSTR FileName);

	//2.����stub.dll
	BOOL LoadStub(LPCSTR dllName);

	//IAT����
	int RvaToFoa(DWORD Rva);
	VOID EncryIAT();
	VOID SetOEP();//����ɵ�OEP�������µ�OEP
	VOID FixDLLReloc();//�޸��Ǵ�����ض�λ
	VOID CopySecData(LPCSTR desSec, LPCSTR srcSec);//����.text���������ݵ�.pack
	VOID XOREncrySec(LPCSTR secName);//����ĳ������

	//3.��stub��.text���Ƶ�������.pack��������OEP��.pack
	VOID AddSection(LPCSTR desSec, LPCSTR srcSec);
	//ѹ��
	VOID lz4Compress(const char* SectionName);
	//4.�������ļ�
	BOOL SaveFile(LPCWSTR FileName);
	VOID FixReloc();//�޸�.nreloc���ض�λ
	VOID DealWithTLS();//����TLS
};


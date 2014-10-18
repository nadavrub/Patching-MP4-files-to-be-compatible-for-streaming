// PatchMoov.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "windows.h"

#define SWAPBYTEORDER32(num) ((num & 0xff000000) >> 24) | ((num & 0x00ff0000) >> 8) | ((num & 0x0000ff00) << 8) | ((num & 0x000000ff) << 24)

namespace MP4 {
	#pragma pack(push, 1)

	struct ATOM {
		enum { ATOM64 = 0x01000000 };
		UINT64	size;
		union {
			UINT uuid;
			CHAR name[4];
		} type;

		HRESULT Decode(IN PBYTE pStream, IN OUT UINT64& qwBytes);
	};

	struct BASE {
		ULONG	btVersion	:  8;
		ULONG	Flags		: 24;

		HRESULT Decode(IN PBYTE pStream, IN OUT UINT64& qwBytes);
	};

	struct TABLE : BASE {
		ULONG ulEntriesNum;

		HRESULT Decode(IN PBYTE pStream, IN OUT UINT64& qwBytes);
	};

	struct STCO : TABLE {
		enum { UUID = 'octs' };

		PULONG pRVAs;

		HRESULT Decode(IN PBYTE pStream, IN OUT UINT64& qwBytes);

		STCO() : pRVAs(0) {}
		~STCO() { free(pRVAs); }
	};

	#pragma pack(pop)
	
	PBYTE FindATOM(IN PBYTE pStream, IN UINT64 qwBytes, IN const UINT uuid, OUT ATOM& atom);


	HRESULT ATOM::Decode(IN PBYTE pStream, IN OUT UINT64& qwBytes) {
		PBYTE pSize = (PBYTE)&size;
		type.uuid	= *(PUINT)(pStream + 4);

		if(ATOM64 == *(PUINT)pStream) {
			pSize[0] = pStream[15];
			pSize[1] = pStream[14];
			pSize[2] = pStream[13];
			pSize[3] = pStream[12];
			pSize[4] = pStream[11];
			pSize[5] = pStream[10];
			pSize[6] = pStream[9];
			pSize[7] = pStream[8];
			qwBytes = 16;
		} else {
			size = SWAPBYTEORDER32(*(PULONG)pStream);
			qwBytes = 8;
		}
		return S_OK;
	}

	HRESULT BASE::Decode(IN PBYTE pStream, IN OUT UINT64& qwBytes) {
		if(qwBytes < sizeof(BASE))
			return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
		btVersion			= pStream[0];
		Flags				= (pStream[1] << 16) || (pStream[2] << 8) || pStream[3];
		qwBytes				= sizeof(BASE);
		return S_OK;
	}

	HRESULT TABLE::Decode(IN PBYTE pStream, IN OUT UINT64& qwBytes) {
		HRESULT hr;
		if(qwBytes < sizeof(TABLE))
			return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
		if(FAILED(hr = __super::Decode(pStream, qwBytes)))
			return hr;
		ulEntriesNum	= SWAPBYTEORDER32(*(PULONG)(pStream + qwBytes));
		qwBytes			= sizeof(TABLE);
		return S_OK;
	}

	HRESULT STCO::Decode(IN PBYTE pStream, IN OUT UINT64& qwBytes) {
		HRESULT hr;
		if(qwBytes < sizeof(STCO))
			return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
		if(FAILED(hr = __super::Decode(pStream, qwBytes)))
			return hr;
		if(0 == (pRVAs = (PULONG)malloc(ulEntriesNum * sizeof(*pRVAs))))
			return E_OUTOFMEMORY;
		const PULONG pPtr = (PULONG)(pStream + qwBytes);
		for(ULONG i = 0; i < ulEntriesNum; i++)
			pRVAs[i] = SWAPBYTEORDER32(pPtr[i]);
		return S_OK;
	}

	PBYTE FindATOM(IN PBYTE pStream, IN UINT64 qwBytes, IN const UINT uuid, OUT ATOM& atom) {
		const PBYTE	pEnd	= pStream + qwBytes;
		PBYTE	pRet = 0;
		ATOM	atomNext;
		while(pStream < pEnd) {
			ZeroMemory(&atomNext, sizeof(atomNext));
			UINT64 qwProcessedBytes = (UINT64)(pEnd - pStream);
			if(FAILED(atomNext.Decode(pStream, qwProcessedBytes)))
				break;
			if(uuid == atomNext.type.uuid) {
				atom = atomNext;
				return pStream;
			}
			switch(atomNext.type.uuid) {
			case 'voom':
			case 'kart':
			case 'aidm':
			case 'fnim':
			//case 'fnid':
			case 'lbts':
				if(0 != (pRet = FindATOM(pStream + qwProcessedBytes, atomNext.size - qwProcessedBytes, uuid, atom)))
					return pRet;
				break;
			}
			pStream += atomNext.size;
		}
		ZeroMemory(&atom, sizeof(atom));
		return 0;
	}
}


int _tmain(int argc, _TCHAR* argv[])
{
	struct ATOMEX : MP4::ATOM {
		PBYTE pAddr;
	};

	HRESULT hr		= S_OK;
	HANDLE	hFile	= INVALID_HANDLE_VALUE;
	HANDLE	hMap	= 0;
	PBYTE	pbtData	= 0;
	UINT64	qwSize	= 0;
	if(argc != 2) {
		printf("Syntax error.\r\n\tUse PatchMP4Moov.exe %%mp4 file%%\r\n");
		return 0;
	}

	try {
		hFile = CreateFile(argv[1], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
		if(INVALID_HANDLE_VALUE == hFile)
			throw HRESULT_FROM_WIN32(GetLastError());
		hMap = CreateFileMappingW(hFile, 0, PAGE_READWRITE, 0, 0, 0);
		if(0 == hMap)
			throw HRESULT_FROM_WIN32(GetLastError());
		pbtData = (PBYTE)MapViewOfFileEx(hMap, FILE_MAP_WRITE, 0, 0, 0, 0);
		if(0 == pbtData)
			throw HRESULT_FROM_WIN32(GetLastError());
		GetFileSizeEx(hFile, (PLARGE_INTEGER)&qwSize);

		PBYTE	pPtr	= pbtData;
		PBYTE	pEnd;
		ATOMEX	atom, moov, mdat;

		// Find the top level atoms
		if(0 == (moov.pAddr = MP4::FindATOM(pPtr, qwSize, 'voom', moov)))
			throw HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
		if(0 == (mdat.pAddr = MP4::FindATOM(pPtr, qwSize, 'tadm', mdat)))
			throw HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
		if(moov.pAddr < mdat.pAddr) {
			printf("'moov' is in-place, no further processing is needed.\r\n");
			throw S_OK;// No need to change
		}
		pPtr	= moov.pAddr;
		pEnd	= pPtr + moov.size;
		qwSize	= moov.size;
	
		while(0 != (atom.pAddr = MP4::FindATOM(pPtr, qwSize, MP4::STCO::UUID, atom))) {
			MP4::STCO stco;
			qwSize = (UINT64)(pEnd - atom.pAddr) - 8;
			if(FAILED(hr = stco.Decode(atom.pAddr + 8, qwSize)))
				throw hr;
			PULONG pOffsets = (PULONG)(atom.pAddr + 8 + (ULONG)&((MP4::STCO*)0)->pRVAs);
			for(ULONG i = 0; i < stco.ulEntriesNum; i++)
				pOffsets[i] = SWAPBYTEORDER32(stco.pRVAs[i] + moov.size);
			pPtr = atom.pAddr + atom.size;
			qwSize = (UINT64)(pEnd - pPtr);
		}
	
		PBYTE pMoovTmp = (PBYTE)malloc((size_t)moov.size);
		if(0 == pMoovTmp)
			throw E_OUTOFMEMORY;
		CopyMemory(pMoovTmp, moov.pAddr, (size_t)moov.size);
		MoveMemory(mdat.pAddr + (size_t)moov.size, mdat.pAddr, (UINT64)(moov.pAddr - mdat.pAddr));
		CopyMemory(mdat.pAddr, pMoovTmp, (size_t)moov.size);
		free(pMoovTmp);
	} catch (HRESULT _hr) {
		hr = _hr;
	}
	if(0 != pbtData)
		FlushViewOfFile(pbtData, 0);
	if(INVALID_HANDLE_VALUE != hFile)
		FlushFileBuffers(hFile);
	if(0 != pbtData)
		UnmapViewOfFile(pbtData);
	if(0 != hMap)
		CloseHandle(hMap);	
	if(INVALID_HANDLE_VALUE != hFile)
		CloseHandle(hFile);
	if(FAILED(hr))
		printf("Failed with 0x%.8x\r\n", hr);
	else
		printf("SUCCEEDED!!!\r\n");
	return hr;
}


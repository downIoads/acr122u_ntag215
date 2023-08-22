#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winscard.h>

typedef struct _SCARD_DUAL_HANDLE {
	SCARDCONTEXT hContext;
	SCARDHANDLE hCard;
} SCARD_DUAL_HANDLE, * PSCARD_DUAL_HANDLE;


void PrintHex(LPCBYTE pbData, DWORD cbData)
{
	DWORD i;
	for (i = 0; i < cbData; i++)
	{
		wprintf(L"%02x ", pbData[i]);
	}
	wprintf(L"\n");
}

BOOL SendRecvReader(PSCARD_DUAL_HANDLE pHandle, const BYTE* pbData, const UINT16 cbData, BYTE* pbResult, UINT16* pcbResult)
{
	BOOL status = FALSE;
	DWORD cbRecvLenght = *pcbResult;
	LONG scStatus;

	wprintf(L"> ");
	PrintHex(pbData, cbData);

	scStatus = SCardTransmit(pHandle->hCard, NULL, pbData, cbData, NULL, pbResult, &cbRecvLenght);
	if (scStatus == SCARD_S_SUCCESS)
	{
		*pcbResult = (UINT16)cbRecvLenght;

		wprintf(L"< ");
		PrintHex(pbResult, *pcbResult);

		status = TRUE;
	}
	else wprintf(L"%08x\n", scStatus);

	return status;
}

BOOL OpenReader(LPCWSTR szReaderName, PSCARD_DUAL_HANDLE pHandle)
{
	BOOL status = FALSE;
	LONG scStatus;
	DWORD dwActiveProtocol;

	scStatus = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &pHandle->hContext);
	if (scStatus == SCARD_S_SUCCESS)
	{
		scStatus = SCardConnect(pHandle->hContext, szReaderName, SCARD_SHARE_SHARED, SCARD_PROTOCOL_Tx, &pHandle->hCard, &dwActiveProtocol);
		if (scStatus == SCARD_S_SUCCESS)
		{
			status = TRUE;
		}
		else
		{
			SCardReleaseContext(pHandle->hContext);
		}
	}

	return status;
}

void CloseReader(PSCARD_DUAL_HANDLE pHandle)
{
	SCardDisconnect(pHandle->hCard, SCARD_LEAVE_CARD);
	SCardReleaseContext(pHandle->hContext);
}

int FastReadFromTag(BYTE fromPage, BYTE toPage, UINT16 replySize) {
	// ff 00 00 00 05 (communicate with pn532 and 05 byte command will follow)
	// d4 (data exchange command)
	// 42 (InCommunicateThru)
	// 3a (FastRead) [page 39 of ntag21x document]
	const BYTE APDU_Read[] = { 0xff, 0x00, 0x00, 0x00, 0x05, 0xd4, 0x42, 0x3a, fromPage, toPage };
	UINT16 apduReadLength = sizeof(APDU_Read) / sizeof(APDU_Read[0]);

	SCARD_DUAL_HANDLE hDual;
	BYTE Buffer[600];    // im using ntag215 so this will be enough even when reading entire user data
	UINT16 cbBuffer;	// usually will be 2 (e.g. response 90 00 for success)

	// my Laptop:	"ACS ACR122U PICC Interface 0"
	// my PC:		"ACS ACR122 0"
	if (OpenReader(L"ACS ACR122 0", &hDual))
	{
		cbBuffer = 600;	// in case someone wants to read all user memory of ntag215 (increase this for ntag216!)
		if (SendRecvReader(&hDual, APDU_Read, apduReadLength, Buffer, &cbBuffer))
		{

			// check for success (success code is stored after the data read, so read the last two bytes of the buffer)
			if (!(Buffer[replySize -2] == 0x90 && Buffer[replySize-1] == 0x00)) {
				wprintf(L"test\n");
				wprintf(Buffer[replySize - 2]);
				wprintf(Buffer[replySize - 1]);
				
				CloseReader(&hDual);
				wprintf(L"Error code received. Aborting..\n");
				return 1;
			}
			else {
				wprintf(L"\nSuccessfully fast-read the specified page interval.\n");
			}
		}
		else {
			wprintf(L"Failed to read pages.");
			CloseReader(&hDual);
			return 1;
		}

		CloseReader(&hDual);
	}
	else {
		wprintf(L"Failed to find NFC reader.\n");
	}

	return 0;
}

int main() {
	BYTE fromPage = 0x04;
	BYTE toPage = 0x43;	//0x81 is last user data page
	if (toPage > 0x86 || fromPage < 0x00 || toPage < fromPage) {	//0x86 is last page of ntag215 (0x2C for ntag213, 0xE6 for ntag216)
		wprintf(L"Invalid page range.\n");
		return 1;
	}

	UINT16 replySize = (toPage - fromPage + 1) * 4 + 5;		// 4 bytes per page, +5 because prefix d5 43 00 and suffix 90 00
	// page 29 of PN532 application note says "Maximum up to 256 data bytes can be transmitted between the reader and the the PN532(short APDU)" (nice typo btw)
	if ( (toPage-fromPage) > 62 ) {
		wprintf(L"Max byte transmission workaround required. TODO\n");
		return 1;
	}

	FastReadFromTag(fromPage, toPage, replySize);

	return 0;
}

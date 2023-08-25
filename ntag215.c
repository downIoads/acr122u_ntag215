// #include <stdbool.h>
#include <memory.h>
#include <stdio.h>
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

/*
int WriteToTag(const BYTE* Msg, BYTE block, bool allowUnsafeTargetBlocks) {
	bool allowedBlock = false;
	
	BYTE APDU_Write[5 + 16] = { 0xff, 0xd6, 0x00, block, 0x10 };	// base command 5 bytes + msg up to 16 bytes
	UINT16 apduWriteBaseLength = sizeof(APDU_Write) / sizeof(APDU_Write[0]);
	memcpy(APDU_Write + 5, Msg, 16);

	// use line below for DEFAULT KEY A
	const BYTE APDU_LoadDefaultKey[] = { 0xff, 0x82, 0x00, 0x00, 0x06, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	const BYTE APDU_Authenticate_Block[] = { 0xff, 0x86, 0x00, 0x00, 0x05, 0x01, 0x00, block, 0x60, 0x00 };

	SCARD_DUAL_HANDLE hDual;
	BYTE Buffer[32];
	UINT16 cbBuffer;	// usually will be 2 (e.g. response 90 00 for success)

	// preparations are complete
	printf("Writing Hex:\n");
	for (UINT16 i = 0; i < 21; ++i) {		// first few chars are not part of message that will be written so skip printing them
		printf("0x%02X ", APDU_Write[i]);
	}
	printf("\n");


	// my Laptop:	"ACS ACR122U PICC Interface 0"
	// my PC:		"ACS ACR122 0"
	if (OpenReader(L"ACS ACR122 0", &hDual))
	{

		cbBuffer = 2;
		if (SendRecvReader(&hDual, APDU_LoadDefaultKey, sizeof(APDU_LoadDefaultKey), Buffer, &cbBuffer))
		{
			wprintf(L"Default Key A has been loaded.\n");
		}
		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0x90 && Buffer[1] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			free(APDU_Write);
			return 1;
		}

		cbBuffer = 2;
		if (SendRecvReader(&hDual, APDU_Authenticate_Block, sizeof(APDU_Authenticate_Block), Buffer, &cbBuffer))
		{
			wprintf(L"Block has been authenticated.\n");
		}
		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0x90 && Buffer[1] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			free(APDU_Write);
			return 1;
		}

		cbBuffer = 2;
		if (SendRecvReader(&hDual, APDU_Write, 23, Buffer, &cbBuffer))
		{
			wprintf(L"Data has successfully been written to the block!\n");
		}
		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0x90 && Buffer[1] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			free(APDU_Write);
			return 1;
		}

		CloseReader(&hDual);
	}
	else {
		wprintf(L"Failed to find NFC reader.\n");
	}

	//free(APDU_Write);

	return 0;
}
*/

int FastReadFromTag(BYTE fromPage, BYTE toPage, UINT16 replySize, BYTE* resultArray, int resultArrayCardinality) {
	// ff 00 00 00 05 (communicate with pn532 and 05 byte command will follow)
	// d4 (data exchange command)
	// 42 (InCommunicateThru)
	// 3a (FastRead) [page 39 of ntag21x document]
	const BYTE APDU_Read[] = { 0xff, 0x00, 0x00, 0x00, 0x05, 0xd4, 0x42, 0x3a, fromPage, toPage };
	UINT16 apduReadLength = sizeof(APDU_Read) / sizeof(APDU_Read[0]);

	SCARD_DUAL_HANDLE hDual;
	BYTE Buffer[984];    // im using ntag215 so this will be enough even when reading entire user data
	UINT16 cbBuffer;	// usually will be 2 (e.g. response 90 00 for success)

	// my Laptop:	"ACS ACR122U PICC Interface 0"
	// my PC:		"ACS ACR122 0"
	if (OpenReader(L"ACS ACR122 0", &hDual))
	{
		cbBuffer = replySize;	// limit of pn532 frames
		if (SendRecvReader(&hDual, APDU_Read, apduReadLength, Buffer, &cbBuffer))
		{
			// check for success (success code is stored after the data read, so read the last two bytes of the buffer)
			if (!(Buffer[replySize-2] == 0x90 && Buffer[replySize-1] == 0x00)) {

				CloseReader(&hDual);
				wprintf(L"Error code received. Aborting..\n");
				return 1;
			}
			else {
				wprintf(L"Successful FastRead.\n\n");
				// store results (without APDU prefix/suffix) in resultArray (but dont just store data at the start, instead store it corresponding to the position where it was on the tag)
				memcpy_s(resultArray + fromPage*4, resultArrayCardinality-fromPage, &Buffer[3], replySize-2);			// ignore prefix d5 43 00 and suffix 90 00

			}
		}
		else {
			wprintf(L"Failed to read pages.\n");
			CloseReader(&hDual);
			return 1;
		}

		CloseReader(&hDual);
		return 0;
	}

	else {
		wprintf(L"Failed to find NFC reader.\n");
		return 1;
	}
}

int FastRead() {
	// define what you want to read
	BYTE fromPage = 0x00;
	BYTE toPage = 0x86;		//0x86 is last ntag215 page, 0x81 is its last user data page
	
	if (toPage > 0x86 || fromPage < 0x00 || toPage < fromPage) {	//0x86 is last page of ntag215 (0x2C for ntag213, 0xE6 for ntag216)
		wprintf(L"Invalid page range.\n");
		return 1;
	}
	int fastReadReturnCode;	// 0 on success, not 0 on not success
	int pagesReadAmount = toPage - fromPage + 1;
	
	BYTE tagContent[984];	// fill array with ascii '?' and only values that are read from tag are replaced in their corresponding position
	int tagContentCardinality = 984;
	// ntag215 can hold 540 bytes in total (ntag216 984 bytes), so allocate all these bytes and put value 63 (ascii '?')
	for (int e = 0; e < tagContentCardinality; ++e) {
		tagContent[e] = 63;
	}
	
	// PN532 can only transfer 256 bytes at once (page 29 of PN532 application note):
	// "Maximum up to 256 data bytes can be transmitted between the reader and the the PN532(short APDU)" (nice typo btw)
	// therefore split the function call into 4 (so that even with ntag216 it will work guaranteed) if more than 62 pages are read
	BYTE int1 = 0x00, int2 = 0x00, int3 = 0x00, int4 = 0x00;
	if (pagesReadAmount > 62) {
		int toAdd = pagesReadAmount % 4;	// add a few imaginary pages if necessary so that it can evenly and cleanly (no float) be divided into 4 calls
		int imagPagesReadAmount;
		if (toAdd != 0) {
			imagPagesReadAmount = pagesReadAmount + (4 - toAdd);
		}
		else {
			imagPagesReadAmount = pagesReadAmount;
		}
		int stepSize = imagPagesReadAmount / 4;		// int / int gives int
		
		// now do the 4 function calls
		BYTE start = fromPage;
		for (int c = 0; c < 4; ++c) {
			// ensure that you never try to read past the last possible page even with the made-up added pages for interval splitting
			int end;
			if (start + stepSize > toPage) {
				end = toPage;
			}
			else {
				end = start + stepSize;
			}

			// calculate expected reply size (amountPages * bytesPerPage + 5 (3 prefix apdu and 2 suffix apdu status)
			int replySize = (end - start + 1) * 4 + 5;


			fastReadReturnCode = FastReadFromTag(start, end, replySize, &tagContent, tagContentCardinality);
			if (fastReadReturnCode != 0) {
				wprintf(L"Error encountered. Terminating..\n");
				return 1;
			}
			start += stepSize+1;
		}

	}
	else {
		UINT16 replySize = pagesReadAmount * 4 + 5; // 3 prefix apdu and 2 status apdu
		fastReadReturnCode = FastReadFromTag(fromPage, toPage, replySize, &tagContent, tagContentCardinality);
		if (fastReadReturnCode != 0) {
			wprintf(L"Error encountered. Terminating..\n");
			return 1;
		}
	}
	
	// print data that was read from tag and stored in array
	wprintf(L"---------------\nRead content:\n\n");
	for (int j = fromPage; j < toPage + 1; ++j) {
		wprintf(L"Page %02x: 0x%02x 0x%02x 0x%02x 0x%02x \n", j, tagContent[j*4], tagContent[j*4 + 1], tagContent[j*4 + 2], tagContent[j*4 + 3]);
	}
	

	return 0;
}

int main() {
	int status = FastRead();

	return status;

}

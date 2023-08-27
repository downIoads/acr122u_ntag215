// #include <stdbool.h>
#include <memory.h>
#include <stdio.h>
#include <windows.h>
#include <winscard.h>
#include <malloc.h>

#define LEN(arr) ((UINT16) (sizeof (arr) / sizeof (arr)[0]))	// easier determining of sizes

typedef struct _SCARD_DUAL_HANDLE {
	SCARDCONTEXT hContext;
	SCARDHANDLE hCard;
} SCARD_DUAL_HANDLE, * PSCARD_DUAL_HANDLE;

struct NDEFShortRecord {
	BYTE RECORD_INFO;	// i mean the byte for MB ME CF SR IL TNF
	BYTE TYPE_LENGTH;
	BYTE PAYLOAD_LENGTH;
	BYTE TYPE;
	const BYTE* PAYLOAD;
};

void NewNDEFShortRecord(struct NDEFShortRecord* record, BYTE recInfo, BYTE typeLen, BYTE payloadLen, BYTE type, const BYTE* payload) {
	record->RECORD_INFO = recInfo;	// 0x91 for first record (if multiple records exist), 0x11 for every record in between and 0x51 for last record (if multiple records exist). use 0xD1 if its exactly one record.
	record->TYPE_LENGTH = typeLen;	// always 0x01 for text
	record->PAYLOAD_LENGTH = payloadLen+3;	// actual text length+3 (for 3 bytes language description prefix)
	record->TYPE = type;					// 0x54 for text
	
	// allocate memory for PAYLOAD data
	record->PAYLOAD = (BYTE*)malloc((payloadLen) * sizeof(BYTE));
	if (record->PAYLOAD) {	// make sure malloc didnt return nllptr
		memcpy(record->PAYLOAD, payload, payloadLen * sizeof(BYTE));
	}
	
}

struct NDEFMessage {
	BYTE NDEF_MESSAGE_START;	// 0x03 (T)
	BYTE NDEF_MESSAGE_LENGTH;	// amount of bytes that follow until NDEF_MESSAGE_END (L)
	BYTE NDEF_MESSAGE_END;		// 0xFE
};

void NewNDEFMessage(struct NDEFMessage* message, BYTE msgStart, BYTE msgLen, BYTE msgEnd) {
	message->NDEF_MESSAGE_START = msgStart;
	message->NDEF_MESSAGE_LENGTH = msgLen;
	message->NDEF_MESSAGE_END = msgEnd;
}

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

int WriteToTag(BYTE toPage, BYTE* data) {
	// ff 00 00 00 05 (communicate with pn532 and 05 byte command will follow)
	// d4 (data exchange command)
	// 42 (InCommunicateThru)
	// a2 (Write) [page 41 of ntag21x document]
	BYTE APDU_Write[9 + 4] = { 0xff, 0x00, 0x00, 0x00, 0x08, 0xd4, 0x42, 0xa2, toPage };
	memcpy_s(APDU_Write + 9, 4, data, 4);

	SCARD_DUAL_HANDLE hDual;
	BYTE Buffer[5];
	UINT16 cbBuffer;	// usually will be 2 (e.g. response 90 00 for success)

	// my Laptop:	"ACS ACR122U PICC Interface 0"
	// my PC:		"ACS ACR122 0"
	if (OpenReader(L"ACS ACR122 0", &hDual))
	{
		cbBuffer = 5; // 5 byte reply expected
		if (SendRecvReader(&hDual, APDU_Write, 13, Buffer, &cbBuffer))
		{

			// check for success (success code is stored after the data read, so read the last two bytes of the buffer)
			if (!(Buffer[3] == 0x90 && Buffer[4] == 0x00)) {

				CloseReader(&hDual);
				wprintf(L"Error code received. Aborting..\n");
				return 1;
			}

			else {
				wprintf(L"Successful Write.\n\n");
			}
		}
		else {
			wprintf(L"Failed to write to page.\n");
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

// writes 0x00 0x00 0x00 0x00 into every page from 0x04 - 0x81 (NTAG215 ONLY. dont use this with other Ntags...)
int DeleteAllUserData() {
	BYTE Msg[4] = { 0x00, 0x00, 0x00, 0x00 };
	for (int j = 0x04; j < 0x82; ++j) {
		int status = WriteToTag(j, &Msg);
		if (status != 0) {
			wprintf(L"Error occurred while writing. Terminating.. \n");
		}

	}
	return 0;
}

// defines 3 Short Records (text), then creates valid NDEF message that contains these records
int NDEFMessageGenerator() {
	// will put put before start of payload to communicate in which language the text is
	const BYTE LanguagePrefix[3] = { 0x02, 0x65, 0x6E };	// en = english text record

	// SHORT RECORD 1
	struct NDEFShortRecord r1;	
	const BYTE r1Payload[3] = { 0x61, 0x62, 0x63 }; // "abc"
	NewNDEFShortRecord(&r1, 0x91, 0x01, 0x03, 0x54, &r1Payload);
	const UINT16 r1Size = sizeof(r1);

	// SHORT RECORD 2
	struct NDEFShortRecord r2;
	const BYTE r2Payload[4] = { 0x74, 0x65, 0x73, 0x74 }; // "test"
	NewNDEFShortRecord(&r2, 0x11, 0x01, 0x04, 0x54, &r2Payload);
	const UINT16 r2Size = sizeof(r2);
	
	// SHORT RECORD 3
	struct NDEFShortRecord r3;
	const BYTE r3Payload[2] = { 0x67, 0x67 }; // "gg"
	NewNDEFShortRecord(&r3, 0x51, 0x01, 0x02, 0x54, &r3Payload);
	const UINT16 r3Size = sizeof(r3);

	// --------------
	// now before ndef message can be created calculate its length
	const BYTE ndefMsgLen = r1.PAYLOAD_LENGTH + r2.PAYLOAD_LENGTH + r3.PAYLOAD_LENGTH + 3*4; // 3 records with 4 byte additional info (RECORD_INFO, TYPE_LENGTH, PAYLOAD_LENGTH, TYPE)
	// --------------

	// NDEF MESSAGE
	struct NDEFMessage ndefMsg;
	NewNDEFMessage(&ndefMsg, 0x03, ndefMsgLen, 0xFE);
	
	// --------------
	// now before full ndef message array can be created and filled determine its total size in bytes
	BYTE NDEFMessageArraySize = ndefMsgLen + 3;	// +3 because 0x03 0xLEN 0xFE
	// printf("Reserving 0x%02x bytes \n", NDEFMessageArraySize);
	// --------------
	
	// dynamically allocate enough space for the array that will hold the entire NDEF message
	BYTE* NDEFMessageArrayPtr = (BYTE*)malloc(NDEFMessageArraySize * sizeof(BYTE));
	if (!NDEFMessageArrayPtr) {
		wprintf(L"Failed to allocate memory. Terminating..");
		return 1;
	}

	// fill the NDEF message array
	int indexCounter = 0;
	//		step 1: add T of TLV (all records together form the V of TLV)
	memcpy(NDEFMessageArrayPtr, &ndefMsg.NDEF_MESSAGE_START, 1);
	indexCounter += 1;
	//		step 2: add L of TLV
	memcpy(NDEFMessageArrayPtr+indexCounter, &ndefMsgLen, 1);
	indexCounter += 1;

	//		step 3: add all records
	//				record 1
	memcpy(NDEFMessageArrayPtr+indexCounter, &r1, r1Size - 1);					// copy everything (4 bytes) except last byte (5th byte) with is the pointer to payload
	indexCounter += 4;

	memcpy(NDEFMessageArrayPtr+indexCounter, &LanguagePrefix, 3);				// copy language prefix bytes
	indexCounter += 3;

	memcpy(NDEFMessageArrayPtr+indexCounter, r1.PAYLOAD, r1.PAYLOAD_LENGTH-3);	// now copy actual payload data
	indexCounter += r1.PAYLOAD_LENGTH-3;
	
	//				record 2
	memcpy(NDEFMessageArrayPtr + indexCounter, &r2, r2Size - 1);
	indexCounter += 4;

	memcpy(NDEFMessageArrayPtr + indexCounter, &LanguagePrefix, 3);
	indexCounter += 3;

	memcpy(NDEFMessageArrayPtr + indexCounter, r2.PAYLOAD, r2.PAYLOAD_LENGTH-3);
	indexCounter += r2.PAYLOAD_LENGTH-3;

	//				record 3
	memcpy(NDEFMessageArrayPtr + indexCounter, &r3, r3Size - 1);
	indexCounter += 4;

	memcpy(NDEFMessageArrayPtr + indexCounter, &LanguagePrefix, 3);
	indexCounter += 3;

	memcpy(NDEFMessageArrayPtr + indexCounter, r3.PAYLOAD, r3.PAYLOAD_LENGTH-3);
	indexCounter += r3.PAYLOAD_LENGTH-3;

	// NDEF MESSAGE END
	memcpy(NDEFMessageArrayPtr + indexCounter, &ndefMsg.NDEF_MESSAGE_END, 1);

	// debug
	for (int j = 0; j < NDEFMessageArraySize; ++j) {
		wprintf(L"%02x ", NDEFMessageArrayPtr[j]);
		if ( (j+1) % 4 == 0) {
			printf("\n");
		}
	}

	// free the dynamically allocated array
	free(r1.PAYLOAD);
	free(r2.PAYLOAD);
	free(r3.PAYLOAD);

	// free(NDEFMessageArrayPtr);		// idk why the program crashes when i try to free this..

	return 0;
}

int main() {
	// READ
	// int status = FastRead();


	// WRITE ONE PAGE
	/*
	BYTE Msg[4] = { 0x00, 0x00, 0x00, 0x00 };
	BYTE Page = 0x0D;	// 0x04 - 0x81 is user data (safe to write). other pages might be dangerous if you are clueless
	if (Page > 0x86 || Page < 0x02) {		// doesnt exist || read only pages
		wprintf(L"Invalid target page.\n");
		return -1;
	}
	int status = WriteToTag(Page, &Msg);
	*/

	// DELETE ALL USER DATA (NTAG215 ONLY)
	// int status = DeleteAllUserData();
	
	int status = NDEFMessageGenerator();

	return status;
}

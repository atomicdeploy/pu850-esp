#include "../ASA0002E.h"
#include "UART.h"

// ESP top level functions _____________________________________________________

void SendESPStatus_ToPU(U8 status, bool force)
{
	// Byte 0-1: ESP State, Byte 2-3: Master State (0: Ready / 1: Busy / 2: Booting)
	const U32 parm = PackU32(0, eMasterStatus, 0, status);

	pinMode(GWP, OUTPUT); digitalWrite(GWP, status == Ready_ && RemainingUARTBuffer() < UART_THRESHOLD_BUSY ? LOW : HIGH); // Low: Ready, High: Busy

	if (force || status != LastESPStatus)
	{
		SendCommand(ESP_Prefix_Request, ESP_Suffix_Status, parm);
		LastESPStatus = status;
	}
}

U8   Request_GivePUStatus()
{
	if (eMasterStatus == Booting_)
		return eMasterStatus;

	if (WaitForSerialProcessing() == NotOK_)
		return Busy_;

	timeOut = millis();

	eMasterStatus = 0xff;

	U32 lastMillis = 0;

	SendESPStatus_ToPU(Ready_, true);

	while (eMasterStatus == 0xff && (U32)(millis() - timeOut) < 20)
		SerialPortReceiveProcess();

	if (eMasterStatus != 0xff)
		return eMasterStatus;

	#ifdef DebugTools
	WriteToDebug("\n\nWaiting for PU status: ");
	inhibitDebugSend[0] = ESP_Prefix_Request;
	inhibitDebugSend[1] = ESP_Suffix_Status;
	DebugLastDirection = 0xff;
	#endif

	while (eMasterStatus == 0xff && (U32)(millis() - timeOut) < validTimeOut_)
	{
		SerialPortReceiveProcess();

		if (Serial_Available() == 0 && (U32)(millis() - lastMillis) >= 100) {
			lastMillis = millis();
			SendESPStatus_ToPU(Ready_, true);

			#ifdef DebugTools
			WriteToDebug(".");
			#endif
		}

		ESP.wdtFeed();
	}

	#ifdef DebugTools
	WriteToDebug("\nResult = ");
	switch (eMasterStatus)
	{
		case Ready_:	WriteToDebug("Ready");		break;
		case Busy_:	WriteToDebug("Busy");		break;
		case Booting_:	WriteToDebug("Booting");	break;
		case 0xff:	WriteToDebug("Timeout");	break;
		default: 	WriteHexToDebug(eMasterStatus);	break;
	}
	// WriteToDebug("\n");
	DebugLastDirection = 0xff;
	#endif

	if (eMasterStatus == 0xff)
		eMasterStatus = Busy_;

	else if (Serial_Available() > 0) WaitForSerialProcessing();

	return eMasterStatus;
}

void SendResponseCode_ToPU(U8 Prefix, U8 Suffix, U8 Response)
{
	SendCommand(ESP_Prefix_Request, ESP_Suffix_Response, ResultPack(Prefix, Suffix, Response));
}

void Request_Beep(U8 num)
{
	SendCommand(ESP_Prefix_Request, ESP_Suffix_Bell, num);
}

bool Request_GiveBasicData()
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_Request;
	eResponseSuffix = ESP_Suffix_BasicData;

	SendCommand(ESP_Prefix_Request, ESP_Suffix_BasicData, 0);

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
	{
		SerialPortReceiveProcess();
	}

	return (eResponseCode == result_Success && eSuffix == ESP_Suffix_BasicData) ? OK_ : NotOK_;
}

bool Request_GoToChart(U8 Index_ID)
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_Request;
	eResponseSuffix = ESP_Suffix_GoToChart;

	U8 Old_IDIndex = E_IDIndex;

	if (Index_ID != E_IDIndex)
		E_IDIndex = UnDefinedNum_;

	SendCommand(eResponsePrefix, eResponseSuffix, Index_ID);

	while ( (E_IDIndex == UnDefinedNum_ && eResponseCode == result_Undefined) && ((U32)(millis() - timeOut) < validTimeOut_) )
	{
		SerialPortReceiveProcess();
	}

	if (E_IDIndex == UnDefinedNum_)
		E_IDIndex = Old_IDIndex;

	// if (E_IDIndex != Index_ID)
	// 	return NotOK_;

	return eResponseCode == result_Success ? OK_ : NotOK_;
}

void Cancel_FileReceiveOperation(bool terminated = true)
{
	#ifdef DebugTools
	LogTimestamp();
	WriteToDebug("\nCancel_FileReceiveOperation(): (terminated = "); WriteToDebug(terminated ? "true" : "false"); WriteToDebug(")\n");
	WriteToDebug("\nCurrentDataSize  = "); WriteToDebug(String(CurrentDataSize));
	WriteToDebug("\nBulkDetected     = "); WriteToDebug(BulkDetected ? "true" : "false");
	WriteToDebug("\nBulkOverflow     = "); WriteToDebug(BulkOverflow ? "true" : "false");
	WriteToDebug("\neProcessState    = "); WriteToDebug(String(eProcessState));
	WriteToDebug("\nisTimeout        = "); WriteToDebug((U32)(millis() - dataTimeOut) >= 2*validTimeOut_ ? "yes" : "no"); WriteToDebug(", ("); WriteToDebug(String((U32)(millis() - dataTimeOut))); WriteToDebug(" ms)");
	WriteToDebug("\n------------------ ");
	WriteToDebug("\nGWP              = "); WriteToDebug(digitalRead(GWP) == LOW ? "LOW (Ready)" : "HIGH (Busy)");
	WriteToDebug("\nBulkBufferSize   = "); WriteToDebug(String(BulkBufferSize));
	WriteToDebug("\nGetBulkRemain()  = "); WriteToDebug(String(GetBulkRemain()));
	WriteToDebug("\nSerial_Available = "); WriteToDebug(String(Serial_Available()));

	WriteToDebug("\n");
	#endif

	eOldMasterStatus = 0xff;

	if (!BulkDetected && !dataTimeOut && eProcessState != WaitforFile_ && eProcessState != WaitforBulk_) {
		dataTimeOut = 0;
		if (eMasterStatus != Ready_)
			SendESPStatus_ToPU(Ready_, true);
		WaitForSerialProcessing();
		BulkReset(0);
		return;
	}

	if (eProcessState == WaitforFile_ || eProcessState == WaitforBulk_)
		ResetReceiveProcess(err_IncompleteFile);

	digitalWrite(GWP, LOW);									// Set as ready

	eMasterStatus = Busy_;									// Prevent other operations

	timeOut = millis();

	dataTimeOut = 0;

	BulkReset(0);

	const U8 parm = terminated ? 0xff : 0xfe;						// 0xff = terminated, 0xfe = paused due to disconnect (keep progress on PU)

	while ((U32)(millis() - timeOut) < validTimeOut_)
	{
		ESP.wdtFeed();

		SendCommand(ESP_Prefix_FileRequest, ESP_Suffix_SendFile, parm);			// Cancel file transmit operation

		#ifdef DebugTools
		WriteToDebug("\nCancel requested (parm = "); WriteToDebug(String(parm, HEX)); WriteToDebug(")\n");
		#endif

		bool waitingForFinish = false;

		#ifdef DebugTools
		U32 skippedBytes = 0;
		#endif

		while ((U32)(millis() - timeOut) <= 10)
		{
			if (Serial_Available() > 0)
			{
				timeOut = millis();

				waitingForFinish = true;

				#ifdef DebugTools
				skippedBytes++;
				#endif

				const C8 ch = Serial_Read();					// Discard UART buffer if any exist

				if (eProcessState == WaitforBulk_)
				{
					if (ch == EOT) break;
				}
			}
		}

		#ifdef DebugTools
		WriteToDebug("\nCancel waiting ("); WriteToDebug(String(skippedBytes)); WriteToDebug(" bytes)\n");
		#endif

		if (!waitingForFinish) break;
	}

	eMasterStatus = Request_GivePUStatus();							// Wait until a valid response received from PU
}

U32  Request_GiveFileMetadata(const C8* filename)
{
	if (WaitForSerialProcessing() == NotOK_)
		return result_Fail;

	U32 ResourceSize = 0;

	FinalDataSize = 0;
	ResourceDateTime = 0;

	memset(E_FileName, Null_, sizeof(E_FileName));
	strncpy(E_FileName, filename, sizeof(E_FileName));

	if (WriteEPtoPU_Str(ESP_Suffix_TransmitFileName) == NotOK_)		// Send file name to PU
		return result_Fail;

	memset(E_FileName, Null_, sizeof(E_FileName));
	eProductStr = E_FileName;

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_FileRequest;
	eResponseSuffix = ESP_Suffix_SendFileMetadata;

	SendCommand(ESP_Prefix_FileRequest, ESP_Suffix_SendFileMetadata, 0);	// Request file metadata (0 = type of metadata)

	while ( (eResponseCode == result_Undefined || eResponseCode == result_Processing) && ((U32)(millis() - timeOut) < validTimeOut_) )
	{
		SerialPortReceiveProcess();

		if (ResourceSize == 0 && FinalDataSize != 0 && E_FileName[0] == Null_)
			ResourceSize = FinalDataSize;

		if (ResourceSize != 0 && ResourceDateTime != 0 && E_FileName[0] != Null_) {
			FinalDataSize = ResourceSize;				// Received file size stored in FinalDataSize
			return result_Success;
		}
	}

	return eResponseCode;
}

U32  Request_GiveFile(const C8* filename, U32 start, U32 length)
{
	if (WaitForBulkDataComplete() == NotOK_ || WaitForSerialProcessing() == NotOK_)
		return result_Fail;

	timeOut = millis();

	BulkReset(0);									// Set CurrentDataSize = 0

	U8 type = 0;

	if (length) {
		if (WriteEPtoPU_Num(ESP_Suffix_FileSeekOffset, start) == NotOK_)	// Send file seek start position to PU
			return result_Fail;

		if (WriteEPtoPU_Num(ESP_Suffix_FileSeekLength, length) == NotOK_)	// Send file seek length to PU
			return result_Fail;

		type = 1;
	}

	memset(E_FileName, Null_, sizeof(E_FileName));
	strncpy(E_FileName, filename, sizeof(E_FileName));

	if (WriteEPtoPU_Str(ESP_Suffix_TransmitFileName) == NotOK_)			// Send file name to PU
		return result_Fail;

	dataTimeOut = millis();

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_FileRequest;
	eResponseSuffix = ESP_Suffix_SendFile;

	SendCommand(ESP_Prefix_FileRequest, ESP_Suffix_SendFile, type);			// Request from PU to send file

	while ( (eResponseCode == result_Undefined || eResponseCode == result_Processing) && ((U32)(millis() - dataTimeOut) < 2*validTimeOut_) )
	{
		SerialPortReceiveProcess();

		if (eResponseCode == result_Not_Found)
			return eResponseCode;

		if (BulkIsStarted() || eResponseCode == result_Success) {		// CurrentDataSize used as `transmitFileSize`
			BulkDetected = true;
			return result_Success;
		}
	}

	if (eResponseCode == result_Undefined && !CurrentDataSize)
		ResetReceiveProcess(err_IncompleteFile);

	LogMessageInFile("Cancelling File Receive Operation");

	Cancel_FileReceiveOperation();

	return eResponseCode;
}

void Request_ReceiveFile()
{
	/*
	String fileContentBytes = "...";

	U32 transmitFileSize = fileContentBytes.length();

	WriteEPtoPU_Str(ESP_Suffix_TransmitFileName);                                   // send file name to PU
	SendCommand(ESP_Prefix_FileRequest, ESP_Suffix_ReceiveFile, transmitFileSize);  // request from PU for receive file

	if (transmitFileSize >= FBPS_)
	{
		while (transmitFileSize > FBPS_) sendDataFile(FBPS_);
	}
	else if (transmitFileSize > 0)
		sendDataFile(transmitFileSize);
	*/
}

U8   Request_IsWeightHidden()
{
	if ( !ReadEPfromPU_Num(ESP_Suffix_HideWeigh) )
		return UnDefinedNum_;

	return E_ShowHideWeight;
}

bool Request_ShowHideWeight(U8 val)
{
	PushToNumEndPoint(ESP_Suffix_HideWeigh, val);
	return WriteEPtoPU_Num(ESP_Suffix_HideWeigh);
}

void Request_SendingWeight(bool sending)
{
	SendCommand(ESP_Prefix_Request, ESP_Suffix_Weigh, (sending ? 1 : 0));
}

S32  Request_GiveWeight()
{
	if (WaitForSerialProcessing() == NotOK_)
		return UnDefinedNum_;

	timeOut = millis();
	eWeightReady = false;

	SendCommand(ESP_Prefix_Request, ESP_Suffix_Weigh, Once_); // (2 = once)

	while ( !eWeightReady && ((U32)(millis() - timeOut) < validTimeOut_) )
	{
		SerialPortReceiveProcess();
	}

	if ( !eWeightReady )
		return UnDefinedNum_;

	return eWeightValue;
}

U32  Request_GiveReceipt(U32 ReceiptNo)
{
	if (WaitForBulkDataComplete() == NotOK_ || WaitForSerialProcessing() == NotOK_)
		return result_Fail;

	timeOut = millis();

	BulkReset(0);

	memset(E_FileName, Null_, sizeof(E_FileName));

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_Request;
	eResponseSuffix = ESP_Suffix_Receipt;

	SendCommand(ESP_Prefix_Request, ESP_Suffix_Receipt, ReceiptNo);

	while ( (eResponseCode == result_Undefined) && ((U32)(millis() - timeOut) < validTimeOut_) )
	{
		SerialPortReceiveProcess();

		if (eResponseCode == result_Not_Found)
			return eResponseCode;

		if (BulkIsStarted() || eResponseCode == result_Success) {
			BulkDetected = true;
			return result_Success;
		}
	}

	if (eResponseCode == result_Undefined && eSuffix == ESP_Suffix_Bulk_Receipt)
		return result_Processing;

	return eResponseCode;
}

bool Request_SendReportParams_ToPU(U8 NormalOrInterim, U8 ReportFormat, U8 ReportKind)
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_GetNum;
	eResponseSuffix = ESP_Suffix_ReportMode;

	const U32 parm = PackU32(0, NormalOrInterim, ReportFormat, ReportKind);

	SendCommand(eResponsePrefix, eResponseSuffix, parm);

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
	{
		SerialPortReceiveProcess();
	}

	return eResponseCode == result_Success ? OK_ : NotOK_;
}

// const U8 NormalOrInterim = 0; // Normal:  0, Interim: 1
// const U8 ReportFormat    = 1; // Graphic: 0, File: 1
// const U8 ReportKind      = 0; // Daily:   0, Lot:  1, Plate: 2

U32 Request_GiveReport(U16 dateFrom, U16 dateTo)
{
	if (WaitForSerialProcessing() == NotOK_)
		return result_Fail;

	timeOut = millis();

	BulkReset(0);

	memset(E_FileName, Null_, sizeof(E_FileName));

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_Request;
	eResponseSuffix = ESP_Suffix_Report;

	const U32 parm = (U32)(dateTo << 16) | dateFrom;		// Note: date = CompressDate(year, month, day)

	SendCommand(ESP_Prefix_Request, ESP_Suffix_Report, parm);

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
	{
		SerialPortReceiveProcess();

		if (eResponseCode == result_Not_Found)
			return eResponseCode;

		if (BulkIsStarted())
			return result_Success;
	}

	if (eResponseCode == result_Undefined && eSuffix == ESP_Suffix_Bulk_Report)
		return result_Processing;

	return eResponseCode;
}

bool Request_GivePower()
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();
	ePowerReady = false;

	SendCommand(ESP_Prefix_Request, ESP_Suffix_Power, 0);

	while ( !ePowerReady && ((U32)(millis() - timeOut) < validTimeOut_) )
	{
		SerialPortReceiveProcess();
	}

	return ePowerReady ? OK_ : NotOK_;
}

bool Request_Tare(U8 HighPrecision)
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_Request;
	eResponseSuffix = ESP_Suffix_Tare;

	SendCommand(ESP_Prefix_Request, ESP_Suffix_Tare, HighPrecision);

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
	{
		SerialPortReceiveProcess();
	}

	return (eResponseCode == result_Success || eResponseCode == result_Processing) ? OK_ : NotOK_;
}

bool Request_SendMessage_ToPU(const C8 *str, U8 title, U8 buttons, U8 icon)
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	PushToStrEndPoint(ESP_Suffix_MessageStr, str);

	if (WriteEPtoPU_Str(ESP_Suffix_MessageStr) == NotOK_)
		return NotOK_;

	constexpr U8 message_id = 0xff; // 255 = read from E_MessageStr

	SendCommand(ESP_Prefix_Request, ESP_Suffix_MessageBox, PackU32(icon, buttons, title, message_id));

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_Request;
	eResponseSuffix = ESP_Suffix_MessageBox;

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
		SerialPortReceiveProcess();

	return (eResponseCode == result_Success || eResponseCode == result_Processing) ? OK_ : NotOK_;
}

bool Request_SetSystemLanguage(C8 lang)
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	SendCommand(ESP_Prefix_Request, ESP_Suffix_SystemLanguage, PackU32(0,0,0, (U8)lang));

	C8 oldLanguage = E_SystemLanguage;

	E_SystemLanguage = result_Undefined; // Wait for result

	while ( (E_SystemLanguage == result_Undefined) && ((U32)(millis() - timeOut) < validTimeOut_) )
		SerialPortReceiveProcess();

	if ( E_SystemLanguage == lang )
		return OK_;

	E_SystemLanguage = oldLanguage;

	return NotOK_;
}

// Endpoint functions (Num & Str) ______________________________________________

bool WriteEPtoPU_Num(U8 EndPoint)
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_GetNum;
	eResponseSuffix = EndPoint;

	SendCommand(ESP_Prefix_GetNum, EndPoint, PopFromNumEndPoint(EndPoint));

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
	{
		SerialPortReceiveProcess();
	}

	return eResponseCode == result_Success ? OK_ : NotOK_;
}

bool WriteEPtoPU_Num(U8 EndPoint, U32 Value)
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_GetNum;
	eResponseSuffix = EndPoint;

	SendCommand(ESP_Prefix_GetNum, EndPoint, Value);

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
	{
		SerialPortReceiveProcess();
	}

	return eResponseCode == result_Success ? OK_ : NotOK_;
}

bool WriteEPtoPU_Str(U8 EndPoint, bool _wait)
{
	U8 i = 0, j;
	eSuffix = EndPoint;
	PopFromStrEndPoint(EndPoint);

	if (_wait && WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	eSendByte[i++] = SOH;
	eSendByte[i++] = ePrefix = ESP_Prefix_GetStr;
	eSendByte[i++] = eSuffix = EndPoint;

	for (j = 0; j < FCSTS_ ; j++)
	{
		eSendByte[i++] = PopDatStr[j];

		if (PopDatStr[j] == Null_) break;
	}

	eSendByte[i++] = EOT;

	CommandSize = i;

	#if defined(ShellOnSerial) || defined(DebugOnSerial)
	return NotOK_;
	#endif

	for (i = 0; i < CommandSize; i++)
	{
		Serial_Write(eSendByte[i]);

		#ifdef DebugTools
		WriteToDebugSerialTransmit(eSendByte[i]);
		#endif
	}

	if (!_wait) return OK_;

	timeOut = millis();

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_GetStr;
	eResponseSuffix = EndPoint;

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
	{
		SerialPortReceiveProcess();
	}

	return eResponseCode == result_Success ? OK_ : NotOK_;
}

U8   CheckAccessPass_PU(const C8* Str)
{
	if (WaitForSerialProcessing() == NotOK_)
		return result_Fail;

	U8 i = 0;

	eSendByte[i++] = SOH;
	eSendByte[i++] = ePrefix = ESP_Prefix_GetStr;
	eSendByte[i++] = eSuffix = ESP_Suffix_AccessPass;

	for (U8 j = 0; j < FCSTS_ ; j++)
	{
		eSendByte[i++] = Str[j];

		if (Str[j] == Null_) break;
	}

	eSendByte[i++] = EOT;

	CommandSize = i;

	Old_AccessLevel = E_AccessLevel;
	E_AccessLevel = UnknownLevel_;

	timeOut = millis();

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_GetStr;
	eResponseSuffix = ESP_Suffix_AccessPass;

	#if defined(ShellOnSerial) || defined(DebugOnSerial)
	return result_Fail;
	#endif

	for (i = 0; i < CommandSize; i++)
	{
		Serial_Write(eSendByte[i]);

		#ifdef DebugTools
		WriteToDebugSerialTransmit(eSendByte[i]);
		#endif
	}

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
	{
		SerialPortReceiveProcess();
	}

	if (E_AccessLevel == UnknownLevel_)
	{
		if (eResponseCode != result_Success)
			E_AccessLevel = Old_AccessLevel;
		else
		{
			if (eMasterStatus != Ready_ || !ReadEPfromPU_Num(ESP_Suffix_AccessLevel))
				return result_Fail;
		}
	}

	return eResponseCode;
}

bool WriteEPtoPU_DateTime()
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	SendCommand(ESP_Prefix_Request, ESP_Suffix_DateTimeInfo, eCompressDateTime());

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_Request;
	eResponseSuffix = ESP_Suffix_DateTimeInfo;

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
	{
		SerialPortReceiveProcess();
	}

	return eResponseCode == result_Success ? OK_ : NotOK_;
}

bool ReadEPfromPU_Num(U8 EndPoint)
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	eRequestedEP = EndPoint;
	eProductReady = false;

	SendCommand(ESP_Prefix_SetNum, EndPoint, 0);

	while ( !eProductReady && ((U32)(millis() - timeOut) < validTimeOut_) )
	{
		SerialPortReceiveProcess();
	}

	return eProductReady ? OK_ : NotOK_;
}

bool ReadEPfromPU_Str(U8 EndPoint)
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();

	eRequestedEP = EndPoint;
	eProductReady = false;

	SendCommand(ESP_Prefix_SetStr, EndPoint, 0);

	while ( !eProductReady && ((U32)(millis() - timeOut) < validTimeOut_) )
	{
		SerialPortReceiveProcess();
	}

	return eProductReady ? OK_ : NotOK_;
}

bool ReadEPfromPU_DateTime()
{
	if (WaitForSerialProcessing() == NotOK_)
		return NotOK_;

	timeOut = millis();
	eDateTimeReady = false;

	SendCommand(ESP_Prefix_Request, ESP_Suffix_WhatDateTime, /*Gregorian_*/ 0);

	while ( !eDateTimeReady && ((U32)(millis() - timeOut) < validTimeOut_) )
	{
		SerialPortReceiveProcess();
	}

	return eDateTimeReady ? OK_ : NotOK_;
}

void SendNetStatus_ToPU(bool force = false)
{
	currentNetStatus = getNetStatus();

	if (!force && oldNetStatus == currentNetStatus)
		return;

	oldNetStatus = currentNetStatus;

	// WriteEPtoPU_Num(ESP_Suffix_NetStatus);

	SendCommand(ESP_Prefix_GetNum, ESP_Suffix_NetStatus, currentNetStatus);
}

bool PushToNumEndPoint(U8 EndPoint, S32 n)
{
	bool s = true;

	eSuffix = EndPoint;

	if (eSuffix >= '0' && eSuffix <= '9')
		eDatNum[(U8)(eSuffix - '0')] = n;

	else switch (eSuffix) // eSuffix = End Point Address
		{
			case ESP_Suffix_NetFlags:           s = setNetFlags((U8)n);        break;

			// case ESP_Suffix_Station_MacOUI:  E_MacOUI             = (U32)n; break; // wifi_set_macaddr(STATION_IF, &newMACAddress[0]);
			// case ESP_Suffix_Station_MacNIC:  E_MacNIC             = (U32)n; break; // wifi_set_macaddr(STATION_IF, &newMACAddress[0]);

			case ESP_Suffix_Station_IP_Addr:
			{
				struct ip_info sta;
				wifi_get_ip_info(STATION_IF, &sta);
				sta.ip.addr = ReverseOrder((U32)n);
				s = wifi_set_ip_info(STATION_IF, &sta);
				break;
			}

			case ESP_Suffix_Station_SubnetMask:
			{
				struct ip_info sta;
				wifi_get_ip_info(STATION_IF, &sta);
				sta.netmask.addr = ReverseOrder((U32)n);
				s = wifi_set_ip_info(STATION_IF, &sta);
				break;
			}

			case ESP_Suffix_Station_GateWay:
			{
				struct ip_info sta;
				wifi_get_ip_info(STATION_IF, &sta);
				sta.gw.addr = ReverseOrder((U32)n);
				s = wifi_set_ip_info(STATION_IF, &sta);
				break;
			}

			case ESP_Suffix_Station_DNS_Addr1:
			case ESP_Suffix_Station_DNS_Addr2:
			{
				IPAddress dns = ReverseOrder((U32)n);
				U8 num = eSuffix == ESP_Suffix_Station_DNS_Addr1 ? 0 : 1;
				dns_setserver(num, dns); // 0 = Primary DNS, 1 = Alternative DNS
				break;
			}

			case ESP_Suffix_AP_IP_Addr:
			{
				struct ip_info ap;
				wifi_get_ip_info(SOFTAP_IF, &ap);
				ap.ip.addr = ReverseOrder((U32)n);
				s = wifi_set_ip_info(STATION_IF, &ap);
				break;
			}

			case ESP_Suffix_AP_SubnetMask:
			{
				struct ip_info ap;
				wifi_get_ip_info(SOFTAP_IF, &ap);
				ap.netmask.addr = ReverseOrder((U32)n);
				s = wifi_set_ip_info(STATION_IF, &ap);
				break;
			}

			case ESP_Suffix_PortHTTP:           HTTP_Port            =  (U16)n; break;
			case ESP_Suffix_PortTelnet:         Telnet_Port          =  (U16)n; break;

			case ESP_Suffix_BaudRate:           SetBaudRateIndex(n);            break;

			case ESP_Suffix_HideWeigh:          E_ShowHideWeight     =       n; onValueUpdate(eSuffix, (U32)n);  break;
			case ESP_Suffix_IDIndex:            E_IDIndex            =       n; onValueUpdate(eSuffix, (U32)n);  break;

			case ESP_Suffix_AccessLevel:        E_AccessLevel        =   (U8)n; onValueUpdate(eSuffix,  (U8)n);  break;

			case ESP_Suffix_Revision:           RevisionMatch        = (FirstByte(ESP_ProtocolRevision_) == n);  break;

			case ESP_Suffix_SerialNumber:       E_SerialNumber       =       n; onValueUpdate(eSuffix, (U32)n);  break;

			// (TODO:) case ESP_Suffix_MaxSessions:        MAX_SESSIONS         =  (U8)n; break;

			case ESP_Suffix_IsExecutable:       IsExecutable         = (bool)n; onValueUpdate(eSuffix, (bool)n); break;

			default:
				#ifdef DebugTools
				WriteToDebug("\nUnknown EndPointNumPush: "); WriteHexToDebug(eSuffix); WriteToDebug("\n");
				#endif

				// ResetReceiveProcess(err_Suffix_NumberEndPoint_NotDetect);
				s = false;

				break;
		}

	if (EndPoint == eRequestedEP) eProductReady = true;

	return s;
}

S32  PopFromNumEndPoint(U8 EndPoint)
{
	eSuffix = EndPoint;

	if (eSuffix >= '0' && eSuffix <= '9')
		return eDatNum[(U8)(eSuffix - '0')];

	else switch (eSuffix) // eSuffix = End Point Address
		{
			case ESP_Suffix_NetFlags: 	    	return       getNetFlags();
			case ESP_Suffix_NetStatus:          	return       currentNetStatus; // getNetStatus();

			case ESP_Suffix_AP_DevicesConnected:    return       wifi_softap_get_station_num();

			case ESP_Suffix_AP_MacOUI:          	return (U32) getMacAddress(SOFTAP_IF, FirstPart_);
			case ESP_Suffix_AP_MacNIC:          	return (U32) getMacAddress(SOFTAP_IF, SecondPart_);
			case ESP_Suffix_Station_MacOUI:     	return (U32) getMacAddress(STATION_IF, FirstPart_);
			case ESP_Suffix_Station_MacNIC:     	return (U32) getMacAddress(STATION_IF, SecondPart_);

			case ESP_Suffix_Station_IP_Addr:
			{
				struct ip_info sta;
				wifi_get_ip_info(STATION_IF, &sta);
				return ReverseOrder(sta.ip.addr);
			}

			case ESP_Suffix_Station_SubnetMask:
			{
				struct ip_info sta;
				wifi_get_ip_info(STATION_IF, &sta);
				return ReverseOrder(sta.netmask.addr);
			}

			case ESP_Suffix_Station_GateWay:
			{
				struct ip_info sta;
				wifi_get_ip_info(STATION_IF, &sta);
				return ReverseOrder(sta.gw.addr);
			}

			case ESP_Suffix_Station_DNS_Addr1:
			case ESP_Suffix_Station_DNS_Addr2:
			{
				U8 num = (eSuffix == ESP_Suffix_Station_DNS_Addr1) ? 0 : 1;
				return ReverseOrder((U32) dns_getserver(num)); // 0 = Primary DNS, 1 = Alternative DNS
			}

			case ESP_Suffix_AP_IP_Addr:
			{
				struct ip_info ap;
				wifi_get_ip_info(SOFTAP_IF, &ap);
				return ReverseOrder(ap.ip.addr);
			}

			case ESP_Suffix_AP_SubnetMask:
			{
				struct ip_info ap;
				wifi_get_ip_info(SOFTAP_IF, &ap);
				return ReverseOrder(ap.netmask.addr);
			}

			case ESP_Suffix_DHCP_Status:        return	getDHCPStatus();

			case ESP_Suffix_PortHTTP:           return (U16)HTTP_Port;
			case ESP_Suffix_PortTelnet:         return (U16)Telnet_Port;

			case ESP_Suffix_Station_RSSI:       return  (U8)E_RSSI;
			case ESP_Suffix_QuantitySSID:       return      E_QuantitySSID;
			case ESP_Suffix_Ping:               return      E_PingResult;

			case ESP_Suffix_BaudRate:           return      GetBaudRateIndex();

			case ESP_Suffix_IDIndex:            return (U32)E_IDIndex;
			case ESP_Suffix_SerialNumber:       return (U32)E_SerialNumber;
			case ESP_Suffix_Revision:           return PackU32(0, 0, IsDebugEnabled(), ESP_ProtocolRevision_);

			case ESP_Suffix_HideWeigh:          return  (U8)E_ShowHideWeight;

			case ESP_Suffix_AppRunningCount:    return      ws.count();

			case ESP_Suffix_MaxSessions:        return  MAX_SESSIONS; // (TODO)

			default:
				#ifdef DebugTools
				WriteToDebug("\nUnknown EndPointNumPop: "); WriteHexToDebug(eSuffix); WriteToDebug("\n");
				#endif

				// ResetReceiveProcess(err_Suffix_NumberEndPoint_NotDetect);

				return UnDefinedNum_;		// as not in list
		}
}

bool PushToStrEndPoint(U8 EndPoint, const C8* str)
{
	bool s = true;

	eSuffix = EndPoint;

	switch (eSuffix) // Suffix as String End Point
	{
		case ESP_Suffix_AP_SSID:
		{
			memset(flashSettings.ap_ssid, Null_, sizeof(flashSettings.ap_ssid));
			strncpy(flashSettings.ap_ssid, str, FCSTS_);

			struct softap_config config;
			wifi_softap_get_config(&config);
			config.ssid_len = strlen(flashSettings.ap_ssid);
			strncpy((C8*)config.ssid, flashSettings.ap_ssid, sizeof(config.ssid));
			s = wifi_softap_set_config_current(&config);
			break;
		}

		case ESP_Suffix_AP_Pass:
		{
			memset(flashSettings.ap_password, Null_, sizeof(flashSettings.ap_password));
			strncpy(flashSettings.ap_password, str, FCSTS_);

			struct softap_config config;
			wifi_softap_get_config(&config);
			strncpy((C8*)config.password, flashSettings.ap_password, sizeof(config.password));
			s = wifi_softap_set_config_current(&config);
			break;
		}

		case ESP_Suffix_StationSSID:
		{
			memset(flashSettings.sta_ssid, Null_, sizeof(flashSettings.sta_ssid));
			strncpy(flashSettings.sta_ssid, str, FCSTS_);

			struct station_config config;
			wifi_station_get_config(&config);
			strncpy((C8*)config.ssid, flashSettings.sta_ssid, sizeof(config.ssid));
			s = wifi_station_set_config_current(&config);
			break;
		}

		case ESP_Suffix_StationPass:
		{
			memset(flashSettings.sta_password, Null_, sizeof(flashSettings.sta_password));
			strncpy(flashSettings.sta_password, str, FCSTS_);

			struct station_config config;
			wifi_station_get_config(&config);
			strncpy((C8*)config.password, flashSettings.sta_password, sizeof(config.password));
			s = wifi_station_set_config_current(&config);
			break;
		}

		case ESP_Suffix_HostName:
		{
			if (!isValidHostName((const C8*) str) || !WiFi.setHostname(str))
			{
				WriteEPtoPU_Str(eSuffix);
				Request_SendMessage_ToPU("Invalid Value", 0, 0, ID_ErrorIcon_);
				s = false;
				break;
			}

			memset(flashSettings.hostname, Null_, sizeof(flashSettings.hostname));
			strncpy(flashSettings.hostname, str, FCSTS_);
			onValueUpdate(ESP_Suffix_HostName, 0);

			break;
		}

		case ESP_Suffix_MainVersion:
		{
			memset(E_MainVersion, Null_, sizeof(E_MainVersion));
			strncpy(E_MainVersion, str, FCSTS_);
			SSDPDevice.setModelNumber(E_MainVersion);
			break;
		}

		case ESP_Suffix_MessageStr:
		{
			memset(E_MessageStr, Null_, sizeof(E_MessageStr));
			strncpy(E_MessageStr, str, FCSTS_);
			break;
		}

		case ESP_Suffix_TransmitFileName:
		{
			memset(E_FileName, Null_, sizeof(E_FileName));
			strncpy(E_FileName, str, FCSTS_);
			break;
		}

		case ESP_Suffix_UserName:
		{
			memset(E_UserName, Null_, sizeof(E_UserName)); // FCSTS_ + 4
			strncpy(E_UserName, str, FCSTS_ + 3);
			onValueUpdate(eSuffix, 0);
			break;
		}

		case 0xff:						// Same as Str_By_Pointer_ in PU where value is 0xff
		{
			if (eProductStr != nullptr)
			{
				strncpy(eProductStr, str, FCSTS_);
			}
			eProductStr = nullptr;
			break;
		}

		default:
		{
			if (is_between(eSuffix, '0', '9'))		// eSuffix = End Point Address for eDatStr
			{
				const U8 a = (U8)(eSuffix - (U8)'0');
				strncpy(eDatStr[a], str, FCSTS_);
				break;
			}

			#ifdef DebugTools
			WriteToDebug("\nUnknown PushToStrEndPoint: "); WriteHexToDebug(eSuffix); WriteToDebug("\n");
			#endif

			// ResetReceiveProcess(err_Suffix_StringEndPoint_NotDetect);
			s = false;
		}
	}

	if (EndPoint == eRequestedEP) eProductReady = true;

	return s;
}

void PopFromStrEndPoint(U8 EndPoint)
{
	eSuffix = EndPoint;

	memset(PopDatStr, Null_, sizeof(PopDatStr));

	switch (eSuffix) // eSuffix = End Point Address
	{
		case ESP_Suffix_AP_SSID:
		{
			struct softap_config config;
			wifi_softap_get_config(&config);
			strncpy(PopDatStr, reinterpret_cast<const char*>(config.ssid), min((U8)sizeof(PopDatStr), config.ssid_len));
			if (config.ssid_len < sizeof(PopDatStr)) PopDatStr[config.ssid_len] = Null_;
			break;
		}

		case ESP_Suffix_AP_Pass:
		{
			U8 len = 0;
			struct softap_config config;
			wifi_softap_get_config(&config);
			const C8* password = reinterpret_cast<const char*>(config.password);
			strncpy(PopDatStr, password, min((U8)sizeof(PopDatStr), len = (U8)strnlen(password, sizeof(config.password))));
			if (len < sizeof(PopDatStr)) PopDatStr[len] = Null_;
			break;
		}

		case ESP_Suffix_StationSSID:
		{
			U8 len = 0;
			struct station_config config;
			wifi_station_get_config(&config);
			const C8* ssid = reinterpret_cast<const char*>(config.ssid);
			strncpy(PopDatStr, ssid, min((U8)sizeof(PopDatStr), len = (U8)strnlen(ssid, sizeof(config.ssid))));
			if (len < sizeof(PopDatStr)) PopDatStr[len] = Null_;
			break;
		}

		case ESP_Suffix_StationPass:
		{
			U8 len = 0;
			struct station_config config;
			wifi_station_get_config(&config);
			const C8* password = reinterpret_cast<const char*>(config.password);
			strncpy(PopDatStr, password, min((U8)sizeof(PopDatStr), len = (U8)strnlen(password, sizeof(config.password))));
			if (len < sizeof(PopDatStr)) PopDatStr[len] = Null_;
			break;
		}

		case ESP_Suffix_HostName:
		{
			const C8* hostname = wifi_station_get_hostname();
			if (hostname != nullptr) strncpy(PopDatStr, hostname, FCSTS_);
			else strncpy(PopDatStr, flashSettings.hostname, FCSTS_); // wifi_station_hostname
			break;
		}

		case ESP_Suffix_MessageStr:
		{
			strncpy(PopDatStr, E_MessageStr, FCSTS_);
			break;
		}

		case ESP_Suffix_TransmitFileName:
		{
			strncpy(PopDatStr, E_FileName, FCSTS_);
			break;
		}

		case ESP_Suffix_FirmwareHash:
		{
			strncpy(PopDatStr, ESP.getSketchMD5().c_str(), FCSTS_);
			break;
		}

		default:
		{
			if (is_between(eSuffix, '0', '9'))
			{
				const U8 a = (U8)(eSuffix - (U8)'0');
				strncpy(PopDatStr, eDatStr[a], FCSTS_);
				break;
			}

			#ifdef DebugTools
			WriteToDebug("\nUnknown EndPointStrPop: "); WriteHexToDebug(eSuffix); WriteToDebug("\n");
			#endif

			// ResetReceiveProcess(err_Suffix_StringEndPoint_NotDetect);

			memset(PopDatStr, '?', FCSTS_); // as not in list
		}
	}
}

void SendMultiSettings()
{
	U32 s = PackU32(0, 0, (U8)(IsGetPU_LCD ? 1 : 0), (U8)(IsGetPU_Key ? 1 : 0));
	SendCommand(ESP_Prefix_Request, ESP_Suffix_MultiSettings, s);
}

// Bulk Buffer Data functions __________________________________________________

void BulkReset(U32 BulkSize)
{
	// Bulk size
	CurrentDataSize  = BulkSize;
	BulkBufferSize   = BulkSize ? constrain(BulkSize, (U32)FCSTS_+8, (U32)FBPS_*4) : 0;
	FinalDataSize    = 0;

	// Read/write counters
	BulkReadCounter  = 0;
	BulkWriteCounter = 0;

	// Total counters
	BulkTotalRead    = 0;
	BulkTotalWrite   = 0;

	// Clear buffer
	free(BulkBuffer);
	BulkBuffer       = BulkBufferSize == 0 ? NULL : (C8*) malloc(BulkBufferSize * sizeof(C8));
	BulkDetected     = false;

	// Overflow state
	BulkOverflow     = false;

	if (BulkBuffer == NULL) BulkBufferSize = 0; // Buffer allocation failed

	// Clear GWP pin
	BulkIsBusy();
}

void WriteBulkTerminator()
{
	const U16 BulkPreviousIndex = BulkWriteCounter > 0 ? (BulkWriteCounter - 1) % sizeof(BulkBuffer) : sizeof(BulkBuffer) - 1;

	if (BulkBuffer[BulkPreviousIndex] == Null_)
		return;

	BulkBuffer[BulkWriteCounter] = Null_;
}

bool WriteToBulk(C8 ch)
{
	const U16 BulkNextIndex = (BulkWriteCounter + 1) % BulkBufferSize;

	if (BulkNextIndex == BulkReadCounter || !BulkBufferSize) {
		BulkOverflow = true;
		#ifdef DebugTools
		onBulkOverflow(ch);
		#endif
		return false;
	}

	BulkBuffer[BulkWriteCounter] = ch;
	BulkWriteCounter = BulkNextIndex;
	BulkTotalWrite++;

	return true;
}

C8   ReadFromBulk()
{
	const C8 ch = BulkBuffer[BulkReadCounter];
	BulkReadCounter = (BulkReadCounter + 1) % BulkBufferSize;
	BulkTotalRead++;
	return ch;
}

S32  GetBulkRemain()
{
	S32 BulkRemain = BulkWriteCounter - BulkReadCounter;

	while (BulkRemain < 0 && BulkBufferSize)
		BulkRemain += BulkBufferSize;

	return BulkRemain;
}

U8   BulkWriteStringTo(C8* Str, U16 Size)
{
	U8 Counter = 0;

	while (GetBulkRemain() > 0 && Counter < Size)
	{
		Str[Counter] = ReadFromBulk();
		if (Str[Counter] == Null_) break;
		Counter++;
	}

	return Counter;
}

inline bool BulkIsBusy()
{
	if ((eProcessState == WaitforBulk_ || eProcessState == WaitforFile_) && BulkDetected && BulkBufferSize && !BulkOverflow && GetBulkRemain() >= (S32)BulkBufferSize)
	{
		#ifdef DebugTools
		// LogTimestamp(); WriteToDebug("BulkIsBusy: "); WriteToDebug(String(GetBulkRemain())); WriteToDebug(", Serial_Available: "); WriteToDebug(String(Serial_Available())); WriteToDebug("\n");
		#endif

		CanInterruptSerialProcessing(Null_);		// Handles timeout operations and if needed sets pin as Busy, i.e. digitalWrite(GWP, HIGH);
		return true;
	}

	return false;
}

// Master ESP Service Initialize _______________________________________________

bool InitializeUARTService()
{
	U32 timeOut = millis();

	eMasterStatus = Busy_;
	E_SerialNumber = ErrorNum_;

	SendESPStatus_ToPU(Ready_, true);

	if (strnlen(E_MainVersion, sizeof(E_MainVersion)) == 0)
		SendCommand(ESP_Prefix_SetStr, ESP_Suffix_MainVersion, 0);

	while ((U32)(millis() - timeOut) < validTimeOut_)
	{
		do
		{
			yield();
			SerialPortReceiveProcess();

			timeOut += 50;
			delay(50);
		}
		while (Serial_Available() > 0);

		if (eMasterStatus == Ready_ || eMasterStatus == Booting_)
		{
			if (E_SerialNumber == ErrorNum_)
			{
				timeOut = millis();
				E_SerialNumber = UnDefinedNum_;
				Request_GiveBasicData();
			}
			else if (E_SerialNumber < UnDefinedNum_)
			{
				break;
			}
		}

		ESP.wdtFeed();
	}

	WaitForSerialProcessing();

	if (eMasterStatus != Busy_ && !eMultiSettingsSent)
	{
		SendMultiSettings();
	}

	return (E_SerialNumber < UnDefinedNum_);
}

U8   Request_RebootPU(bool wait = false)
{
	if (WaitForSerialProcessing() == NotOK_)
		return result_Fail;

	timeOut = millis();

	eResponseCode = result_Undefined;
	eResponsePrefix = ESP_Prefix_ProgrammingCmd;
	eResponseSuffix = ESP_Suffix_Reboot;

	SendCommand(ESP_Prefix_ProgrammingCmd, ESP_Suffix_Reboot, 0);

	while (eResponseCode == result_Undefined && ((U32)(millis() - timeOut) < validTimeOut_))
	{
		SerialPortReceiveProcess();
	}

	if (eResponseCode != result_Success)
		return eResponseCode == result_Undefined ? result_Fail : eResponseCode;

	eMasterStatus = 0xff;
	initStatus = false;
	eMultiSettingsSent = false;

	if (!wait)
	{
		eMasterStatus = Booting_;
	}
	else
	{
		delay(250);

		ResetReceiveProcess(OK_);

		Request_GivePUStatus();

		if (eMasterStatus != 0xff)
			initStatus = InitializeUARTService();
	}

	return result_Success;
}

U32  GetBaudRateIndex()
{
	switch (BAUD_RATE)
	{
		case 1200   : return BR_1200   ;
		case 2400   : return BR_2400   ;
		case 4800   : return BR_4800   ;
		case 9600   : return BR_9600   ;
		case 14400  : return BR_14400  ;
		case 19200  : return BR_19200  ;
		case 38400  : return BR_38400  ;
		case 56000  : return BR_56000  ;
		case 57600  : return BR_57600  ;
		case 115200 : return BR_115200 ;
	}

	return UndefIndex_;
}

void SetBaudRateIndex(U32 baud)
{
	U32 oldValue = BAUD_RATE;

	switch (baud)
	{
		case BR_1200   : BAUD_RATE = 1200   ;
		case BR_2400   : BAUD_RATE = 2400   ;
		case BR_4800   : BAUD_RATE = 4800   ;
		case BR_9600   : BAUD_RATE = 9600   ;
		case BR_14400  : BAUD_RATE = 14400  ;
		case BR_19200  : BAUD_RATE = 19200  ;
		case BR_38400  : BAUD_RATE = 38400  ;
		case BR_56000  : BAUD_RATE = 56000  ;
		case BR_57600  : BAUD_RATE = 57600  ;
		case BR_115200 : BAUD_RATE = 115200 ;
	}

	if (oldValue != BAUD_RATE)
	{
		// Serial.end();
		Serial.begin(baud);
	}
}

// ESP Primary functions _______________________________________________________

void SendCommand(U8 PrefixIn, U8 SuffixIn, S32 N)
{
	U8 i = 0;
	UnPack32(N);

	eSendByte[i++] = SOH;
	eSendByte[i++] = ePrefix = PrefixIn;
	eSendByte[i++] = eSuffix = SuffixIn;
	eSendByte[i++] = ePart[0];
	eSendByte[i++] = ePart[1];
	eSendByte[i++] = ePart[2];
	eSendByte[i++] = ePart[3];
	eSendByte[i++] = EOT;

	CommandSize = i;

	#if defined(ShellOnSerial) || defined(DebugOnSerial)
	return;
	#endif

	for (i = 0; i < CommandSize; i++)
	{
		Serial_Write(eSendByte[i]);

		#ifdef DebugTools
		if (ePrefix == inhibitDebugSend[0] && eSuffix == inhibitDebugSend[1]) continue;

		WriteToDebugSerialTransmit(eSendByte[i]);
		#endif
	}
}

void SerialPortReceiveProcess()
{
	if (digitalRead(GWP) == HIGH)
		SendESPStatus_ToPU(LastESPStatus);		// If buffer is not full, sets pin as Ready, i.e. digitalWrite(GWP, LOW);

	while (Serial_Available() > 0)
	{
		if (BulkIsBusy()) break;

		const C8 echESP = Serial_Read();

		#ifdef DebugTools
		WriteToDebugSerialReceive(echESP);
		#endif

		switch (eProcessState)
		{
			case WaitforSOH_:
				SOH_Processor(echESP);
				break;

			case WaitforPrefix_:
				ePrefix = echESP;
				eProcessState = WaitforSuffix_;
				break;

			case WaitforSuffix_:
				eSuffix = echESP;
				PrefixProcessor();
				break;

			case Waitfor4Num_:
				x4NumProcessor(echESP);
				break;

			case WaitforBulk_:
				BulkDataProcessor(echESP);
				break;

			case WaitforFile_:
				FileDataProcessor(echESP);
				break;

			case WaitforEOT_:
				EOT_Processor(echESP);
				break;

			default: ResetReceiveProcess(err_State_NotDetect); break;
		}

		if (CanInterruptSerialProcessing(echESP)) break;
	}

	ESP.wdtFeed();
}

bool CanInterruptSerialProcessing(U8 echESP)
{
	digitalWrite(GWP, LastESPStatus == Ready_ && RemainingUARTBuffer() < UART_THRESHOLD_BUSY ? LOW : HIGH);

	if (eProcessState == WaitforFile_ && ((U32)millis() - dataTimeOut) >= 2*validTimeOut_)
	{
		LogMessageInFile("Cancelling File Receive Operation");

		Cancel_FileReceiveOperation();				// Must come before `ResetReceiveProcess(err_IncompleteFile)`

		if (eProcessState == WaitforFile_)
			ResetReceiveProcess(err_IncompleteFile);

		return true;
	}

	if (RemainingUARTBuffer() >= UART_THRESHOLD_BUSY)
		return true;

	switch (eProcessState)
	{
		case WaitforSOH_:
			return true;

		case WaitforBulk_:
			return echESP == ENQ;

		case WaitforFile_:
			return BulkIsBusy(); // CurrentDataSize > 0
	}

	return false;
}

bool WaitForSerialProcessing()
{
	#if defined(ShellOnSerial) || defined(DebugOnSerial)
	return NotOK_;
	#endif

	if (eProcessState == WaitforBulk_)
		return NotOK_;

	if (eProcessState == WaitforFile_ && ((U32)millis() - dataTimeOut) < 2*validTimeOut_)
		return NotOK_;

	timeOut = millis();

	bool SerialProcessActive = false;

	while (eProcessState != WaitforSOH_ && (U32)(millis() - timeOut) < validTimeOut_)
	{
		if (Serial_Available() == 0)
		{
			ESP.wdtFeed();
			continue;
		}

		SerialPortReceiveProcess();

		SerialProcessActive = true;
	}

	if (eProcessState == WaitforSOH_)
		return OK_;

	if (!SerialProcessActive && initStatus && Serial_Available() == 0)
	{
		switch (eProcessState)
		{
			case WaitforPrefix_:
			case WaitforSuffix_:
			case WaitforFourth_:
			case WaitforFile_:
			case WaitforEOT_:
				ResetReceiveProcess(err_Time_Out);

				if (eMasterStatus == Busy_)
					SendESPStatus_ToPU(Ready_, true);

				return OK_;
		}
	}

	return NotOK_;
}

bool WaitForBulkDataComplete()
{
	#if defined(ShellOnSerial) || defined(DebugOnSerial)
	return NotOK_;
	#endif

	if (!BulkDetected) return OK_;

	U16 OldBufferTotal = BulkTotalWrite;

	timeOut = millis();

	while ((U32)(millis() - timeOut) < validTimeOut_)
	{
		if (BulkTotalWrite > OldBufferTotal) {
			OldBufferTotal = BulkTotalWrite;
			timeOut = millis();
		}

		if (BulkIsCompleted() || eProcessState == WaitforSOH_)
			return OK_;

		SerialPortReceiveProcess();
	}

	if (eProcessState == WaitforFile_ && (U32)(millis() - dataTimeOut) >= 2*validTimeOut_)
	{
		LogMessageInFile("Cancelling File Receive Operation");

		Cancel_FileReceiveOperation();
	}

	else if (eProcessState == WaitforBulk_ && (U32)(millis() - timeOut) >= validTimeOut_)
	{
		LogMessageInFile("Cancelling File Receive Operation");

		Cancel_FileReceiveOperation();
	}

	return NotOK_;
}


bool WaitForPU_Ready()
{
	U32	statusTimeOut = millis(),
		lastMillis    = millis();

	while (eMasterStatus != Ready_ && (U32)(millis() - statusTimeOut) < validTimeOut_)
	{
		SerialPortReceiveProcess();

		if (Serial_Available() == 0 && (U32)(millis() - lastMillis) >= 100) {
			lastMillis = millis();
			eMasterStatus = Request_GivePUStatus();
		}

		ESP.wdtFeed();
	}

	return eMasterStatus == Ready_;
}

inline bool IsSerialIdle()
{
	return eProcessState == WaitforSOH_ && Serial_Available() == 0 && !BulkDetected;
}

void SOH_Processor(U8 echESP)
{
	if (echESP == SOH) eProcessState = WaitforPrefix_;
	else ResetReceiveProcess(err_SOH_NotDetect);
}

void PrefixProcessor()
{
	switch (ePrefix)
	{
		case ESP_Prefix_DiagCmd:
		case ESP_Prefix_GetNum:
		case ESP_Prefix_SetNum:
		case ESP_Prefix_GetStr:
		// case ESP_Prefix_GetBulk: // not exist
		case ESP_Prefix_Request:
		case ESP_Prefix_Order:
		case ESP_Prefix_FileOrder:
			eProcessState = Waitfor4Num_;		// Handled by EOT_Processor() later
			eSubProcessState = WaitforFourth_;
			break;

		case ESP_Prefix_SetStr:
			BulkReset(FCSTS_ + 4);			// +3 added, and 1 NULL terminator
			eProcessState = WaitforBulk_;
			break;

		case ESP_Prefix_SendBulk:
			BulkReset(FBPS_ * 4);			// 2KB buffer
			eProcessState = WaitforBulk_;
			#ifdef DebugTools
			WriteToDebug("\n\nBulk Receive Started");
			#endif
			break;

		default:
			ResetReceiveProcess(err_Prefix_NotDetect);
			break;
	}
}

void x4NumProcessor(U8 echESP)
{
	switch (eSubProcessState)
	{
		case WaitforFourth_: eSubProcessState = WaitforThird_; ePart[3] = echESP; break;
		case WaitforThird_: eSubProcessState = WaitforSecond_; ePart[2] = echESP; break;
		case WaitforSecond_: eSubProcessState = WaitforFirst_; ePart[1] = echESP; break;
		case WaitforFirst_:
			eSubProcessState = WaitforFirst_; ePart[0] = echESP;
			eWiFiPack4Byte = PackU32(ePart[3], ePart[2], ePart[1], ePart[0]);
			eProcessState = WaitforEOT_;
			break;
	}
}

void BulkDataProcessor(U8 echESP)
{
	switch (echESP)
	{
		case ENQ:
			timeOut = millis();
			break;

		case EOT:
			BulkDetected = true;

			switch (ePrefix)
			{
				case ESP_Prefix_SetStr:							// bulk EOT detected by string
				{
					memset(PopDatStr, Null_, sizeof(PopDatStr));

					WriteBulkTerminator();

					FinalDataSize = BulkWriteStringTo(PopDatStr, sizeof(PopDatStr));

					if (FinalDataSize > FCSTS_ || BulkOverflow)
						ResetReceiveProcess(err_OutOfStringSize);
					else	ResetReceiveProcess(OK_);

					PushToStrEndPoint(eSuffix, PopDatStr);

					onBulkDataReceived();

					BulkReset(0);

					break;
				}

				case ESP_Prefix_SendBulk:						// bulk EOT detected by bulk
				{
					FinalDataSize = BulkTotalWrite;

					switch (eSuffix)
					{
						case ESP_Suffix_Bulk_Report:
						case ESP_Suffix_Bulk_Receipt:
							if (BulkOverflow)	ResetReceiveProcess(err_OutOfBulkSize);
							else			ResetReceiveProcess(OK_);

							break;

						default: ResetReceiveProcess(err_Suffix_Bulk_NotDetect); break;
					}
					break;
				}

				default: ResetReceiveProcess(err_Suffix_Bulk_NotDetect); break;
			}
			break;

		case SOH:
			ResetReceiveProcess(err_EOT_NotDetect);

			#ifdef DebugTools
			WriteToDebug("\nBulk Data Processor: Unexpected SOH detected\n");
			#endif
			break;

		default:
			dataTimeOut = millis();

			if (WriteToBulk(echESP) == false)
			{
				// Overflow state
				if (ePrefix == ESP_Prefix_SetStr) ResetReceiveProcess(err_OutOfStringSize);
				if (ePrefix == ESP_Prefix_SendBulk) ResetReceiveProcess(err_OutOfBulkSize);

				eProcessState = WaitforEOT_;
			}

			else if (ePrefix == ESP_Prefix_SetStr && echESP == Null_)
				eProcessState = WaitforEOT_;

			break;
	}

	onBulkDataReceived();
}

void FileDataProcessor(U8 echESP)
{
	if (!CurrentDataSize || eProcessState != WaitforFile_ || (U32)(millis() - dataTimeOut) >= 2*validTimeOut_)
	{
		if (!CurrentDataSize) ResetReceiveProcess(err_OutOfBulkSize);

		LogMessageInFile("Cancelling File Receive Operation");

		Cancel_FileReceiveOperation(false);
		return;
	}

	dataTimeOut = millis();

	if (WriteToBulk(echESP) == false)
	{
		LogMessageInFile("Cancelling File Receive Operation");

		Cancel_FileReceiveOperation(); // This will also send err_IncompleteFile
		ResetReceiveProcess(err_OutOfBulkSize);
	}

	if (CurrentDataSize == BulkTotalWrite) // CurrentDataSize used as `transmitFileSize`
	{
		FinalDataSize = BulkTotalWrite;
		BulkDetected = true;

		if (BulkOverflow) ResetReceiveProcess(err_OutOfBulkSize);
		else		  ResetReceiveProcess(OK_);
	}

	onBulkDataReceived();
}

void EOT_Processor(U8 echESP)
{
	if (echESP != EOT)
		ResetReceiveProcess(err_EOT_NotDetect);
	else
	{
		switch (ePrefix)
		{
			case ESP_Prefix_GetNum:
				// WriteEPtoPU_Num(eSuffix);
				if (eSuffix == ESP_Suffix_NetStatus) SendNetStatus_ToPU(true);
				else SendCommand(ESP_Prefix_GetNum, eSuffix, PopFromNumEndPoint(eSuffix));
				ResetReceiveProcess(OK_);
				break;

			case ESP_Prefix_GetStr:
				WriteEPtoPU_Str(eSuffix, false);
				ResetReceiveProcess(OK_);
				break;

			case ESP_Prefix_SetNum:
				PushToNumEndPoint(eSuffix, eWiFiPack4Byte);
				ResetReceiveProcess(OK_);
				break;

			case ESP_Prefix_SetStr:
				BulkDataProcessor(EOT);
				break;

			case ESP_Prefix_SendBulk:
				BulkDataProcessor(EOT);
				break;

			case ESP_Prefix_Order:
				OrderProcessor();
				break;

			case ESP_Prefix_Request:
				switch (eSuffix)
				{
					case ESP_Suffix_Weigh:
						Request_SendingWeight(eSendingWeigh);
						ResetReceiveProcess(OK_);
						break;

					case ESP_Suffix_MultiSettings:
						SendMultiSettings();
						ResetReceiveProcess(OK_);
						break;

					default:
						ResetReceiveProcess(err_Order_NotDetect); // err_Suffix_NotDetect
						break;
				}
				break;

			case ESP_Prefix_FileOrder:
				FileOrderProcessor();
				break;

			case ESP_Prefix_DiagCmd:
				DiagProcessor();
				break;

			default:
				ResetReceiveProcess(err_Prefix_NotDetect);

				#ifdef DebugTools
				WriteToDebug("\nUnkown Prefix: " + String(ePrefix) + ", Suffix: " + String(eSuffix) + "\n");
				#endif
				break;
		}
	}
}

void OrderProcessor()
{
	bool ReceiveOrder = true;
	U16 CompressedDate, CompressedTime;

	switch (eSuffix)
	{
		// Received Weight
		case ESP_Suffix_Weigh:
			eWeightValue = (S32)eWiFiPack4Byte;
			eWeightReady = true;
			// onValueUpdate(eSuffix, eWeightValue);
			break;

		// Received Power
		case ESP_Suffix_Power:
			eBatteryValue = (((U16)ePart[1]) << 8) | ePart[0];			// "Battery voltage= " + ((float)(PackU16(ePart[1], ePart[0])) / 100.0).ToString() + "V");
			ePowerStatus  = (((U16)ePart[3]) << 8) | ePart[2];			// (ePart[3] == 1 ? "Main Power" : "Backup Battery")
			ePowerReady   = true;
			break;

		// Received Beep
		case ESP_Suffix_Bell:
			onBeepReceived((U32)eWiFiPack4Byte);
			break;

		// Received PU status
		case ESP_Suffix_Status:
			eMasterStatus = ePart[2];						// Byte 0-1: ESP State,  Byte 2-3: Master State,    0: Ready / 1: Busy / 2: Booting
			if (eMasterStatus != eOldMasterStatus || eMasterStatus == 0xff)
			{
				eOldMasterStatus = eMasterStatus;
				onValueUpdate(ESP_Suffix_Status, eMasterStatus);
			}
			// no send status to master (prevent recursion)
			break;

		// Received date and time
		case ESP_Suffix_DateTimeInfo:
			CompressedDate = eWiFiPack4Byte >> 16;
			CompressedTime = eWiFiPack4Byte & 0xffff;
			eExpandDateTime(CompressedDate, CompressedTime);			// CalendarType = Gregorian_ (Forced in ESP v2)
			onDateTimeReceived();
			eDateTimeReady = true;
			// onValueUpdate(eSuffix, 0);
			break;

		// Received Session Management Order
		case ESP_Suffix_SessionOrder:
			SessionCommand(ePart[0], ePart[1]);					// Bytes:   0: Command (1: Reserved, 2: Disconnect), 1: SessionIndex (0xff = All)
			break;

		// Received System Language
		case ESP_Suffix_SystemLanguage:
			E_SystemLanguage = FirstByte(eWiFiPack4Byte);
			onValueUpdate(eSuffix, E_SystemLanguage);
			break;

		// Received Response Code
		case ESP_Suffix_Response:
			ResponseCodeProcessor((U8)(eWiFiPack4Byte >> 24), (U8)(eWiFiPack4Byte >> 16), (U16)(eWiFiPack4Byte & 0xffff));	// Bytes:  0-1: ResultCode, 2: Suffix, 3: Prefix
			break;

		// Received message for application (id, title, buttons, icon)
		case ESP_Suffix_MessageBox:
			onMessageReceived(ePart[0], ePart[1], ePart[2], ePart[3]);		// Bytes:   0: MessageIndex (0-254: Index, 255: EP MessageStr), 1: Title  /  2: Buttons  / 3: Icon
			break;

		// Save or Discard Settings
		case ESP_Suffix_ConfirmSettings:
			ConfirmSettings((U8)ePart[0], (U8)ePart[3]);				// Bytes:   0: Yes_/No_/Cancel_, 3: Group to be saved
			break;

		default:
			ReceiveOrder = false;
			ResetReceiveProcess(err_Order_NotDetect);
			break;
	}

	if (ReceiveOrder) ResetReceiveProcess(OK_);
}

void FileOrderProcessor()
{
	bool ReceiveOrder = true;

	switch (eSuffix)
	{
		case ESP_Suffix_SendFile:			// PU sends file to ESP, we are receiving file
			BulkReset(eWiFiPack4Byte);		// CurrentDataSize is used as `transmitFileSize`
			if (!eWiFiPack4Byte) break;		// if file size is zero, do not wait for file
			dataTimeOut = millis();
			eProcessState = WaitforFile_;
			#ifdef DebugTools
			WriteToDebug("\nFile Receive Started, Size = " + String(eWiFiPack4Byte) + " bytes\n");
			#endif
			return;

		case ESP_Suffix_SendFileSize:
			FinalDataSize = eWiFiPack4Byte;		// FinalDataSize is used as file size (0-2^32 bytes i.e. 4 GB)
			break;

		case ESP_Suffix_SendFileDateTime:
			ResourceDateTime = eWiFiPack4Byte;	// 4-bytes: 0-1: Date, 2-3: Time. // Date: 0-4: Day (1..31), 5-8: Month (1..12), 9-15: Year from 1980 (0..127) // Time: 0-4: Second / 2 (0..29), 5-10: Minute (0..59), 11-15: Hour (0..23)
			break;

		case ESP_Suffix_ReceiveFile:			// ESP sends file to PU, we will send file
			// (TODO)
			break;

		default:
			ReceiveOrder = false;
			ResetReceiveProcess(err_Order_NotDetect);
			break;
	}

	if (ReceiveOrder) ResetReceiveProcess(OK_);
}

inline void ResponseCodeProcessor(U8 ePrefixIn, U8 eSuffixIn, U16 eResponseIn)
{
	if (ePrefixIn == eResponsePrefix && eSuffixIn == eResponseSuffix)
		eResponseCode = eResponseIn;

	onResponseReceived(ePrefixIn, eSuffixIn, eResponseIn);
}

void DiagProcessor()
{
	switch (eSuffix)
	{
		case Diag_Suffix_SetLED:
			switch (eWiFiPack4Byte)
			{
				case 0: break; // LED_Off
				case 1: break; // LED_On
				case 2: break; // LED_Blink
			}
			break;

		case Diag_Suffix_DataFlagBits:
			break;

		case Diag_Suffix_Programming_ESP:
			break;

		case Diag_Suffix_Programming_AVR:
			break;

		case Diag_Suffix_ExitProgramming:
			break;

		default:
			ResetReceiveProcess(err_Order_NotDetect);
			break;
	}
}

void _ResetReceiveProcess(U8 result)
{
	eProcessState = WaitforSOH_;
	if (result == OK_ && initStatus && eMasterStatus == Busy_) eMasterStatus = Ready_;
}

// Debugging Tools for Serial __________________________________________________

#ifdef DebugTools
void _ResetReceiveProcess(U8 result, U16 line)
{
	#ifdef DebugTools
	if (result != OK_) ReceiveCommandDebug(result, line);
	else if (DebugLastError == err_SOH_NotDetect) WriteToDebug("\nEnd of SOH_NotDetect\n");
	DebugLastError = result;
	#endif

	_ResetReceiveProcess(result);
}

void WriteHexToDebug(U8 value)
{
	WriteToDebug(String(value, 16).c_str(), 2, '0');
}

void WriteToDebug(const String str)
{
	WriteToDebug(str.c_str());
}

void WriteToDebug(const C8* str)
{
	while (*str != Null_)
	{
		if (DebugCounter < DEBUG_SIZE)
			DebugData[DebugCounter++] = *str++;
		else break;
	}
}

void WriteToDebug(const C8* str, U8 padding, C8 ch)
{
	U8 len = strlen(str);

	while (len < padding)
	{
		WriteToDebug(String(ch));
		len++;
	}

	WriteToDebug(str);
}

inline void ReceiveCommandDebug(U8 result, U16 line)
{
	if (result == err_SOH_NotDetect && DebugLastError == result) return;

	WriteToDebug("\nERROR: ");
	switch (result)
	{

		case err_State_NotDetect                 : WriteToDebug("State_NotDetect");          break;
		case err_SOH_NotDetect                   : WriteToDebug("SOH_NotDetect"); WriteToDebug("\n"); return;
		case err_EOT_NotDetect                   : WriteToDebug("EOT_NotDetect");            break;
		case err_Prefix_NotDetect                : WriteToDebug("Prefix_NotDetect");         break;
		case err_Suffix_Bulk_NotDetect           : WriteToDebug("Bulk_NotDetect");           break;
		case err_Suffix_NumberEndPoint_NotDetect : WriteToDebug("NumberEndPoint_NotDetect"); break;
		case err_Suffix_StringEndPoint_NotDetect : WriteToDebug("StringEndPoint_NotDetect"); break;
		case err_OutOfBulkSize                   : WriteToDebug("OutOfBulkSize");            break;
		case err_OutOfStringSize                 : WriteToDebug("OutOfStringSize");          break;
		case err_IncompleteFile                  : WriteToDebug("IncompleteFile");           break;
		case err_Order_NotDetect                 : WriteToDebug("Order_NotDetect");          break;
		case err_Time_Out                        : WriteToDebug("Time_Out");                 break;
		default                                  : WriteToDebug(String(result));             break;
	}
	WriteToDebug(" (line: ");
	WriteToDebug(String(line));
	WriteToDebug(")\n");
}

void WriteToDebugSerialReceive(C8 ch)
{
	if (DebugLastDirection != 0x01)
	{
		DebugLastDirection = 0x01;
		WriteToDebug("\n\nReceive:\n");
	}

	if (eProcessState == WaitforFile_ || (eProcessState == WaitforBulk_ && ePrefix == ESP_Prefix_SendBulk)) return;

	WriteHexToDebug(ch); WriteToDebug(" ");
}

void WriteToDebugSerialTransmit(C8 ch)
{
	if (DebugLastDirection != 0x02)
	{
		DebugLastDirection = 0x02;
		WriteToDebug("\n\nTransmit:\n");
	}

	WriteHexToDebug(ch); WriteToDebug(" ");
}

void onBulkOverflow(C8 ch)
{
	if (DEBUG_SIZE - DebugCounter < 200)
	{
		DebugCounter = 0;
		DebugLastDirection = 0xff;
		memset(DebugData, Null_, sizeof(DebugData));
		WriteToDebug("(Truncated DebugData)\n");
	}

	WriteToDebug("\nBulk Overflow! ");
	WriteToDebug("Char: ");               WriteHexToDebug(ch);;
	WriteToDebug(", BulkReadCounter: ");  WriteToDebug(String(BulkReadCounter));
	WriteToDebug(", BulkWriteCounter: "); WriteToDebug(String(BulkWriteCounter));
	WriteToDebug(", BulkBufferSize: ");   WriteToDebug(String(BulkBufferSize));
	WriteToDebug(", GetBulkRemain(): ");  WriteToDebug(String(GetBulkRemain()));
	WriteToDebug(", Serial_Available: "); WriteToDebug(String(Serial_Available()));
	WriteToDebug(", eProcessState: ");    WriteToDebug(String(eProcessState));
	WriteToDebug("\n");
}

void _LogMessageInFile(String message, const C8* file, U16 line)
{
	WriteToDebug("\n-----\n"); WriteToDebug(message); WriteToDebug("\n");
	WriteToDebug("\nFile: "); WriteToDebug(file); WriteToDebug(" (line "); WriteToDebug(String(line)); WriteToDebug(")\n-----\n");
}

void LogTimestamp()
{
	WriteToDebug("[ms="); WriteToDebug(String(millis())); WriteToDebug("] ");
}
#endif

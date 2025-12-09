#pragma once

#include "../ASA0002E.h"
#include "../defines.h"

U32 BAUD_RATE = 115200;
#define UART_BUFFER_SIZE 1024
#define UART_THRESHOLD_BUSY 512

#define WaitforSOH_     0
#define WaitforPrefix_  1
#define WaitforSuffix_  2
#define Waitfor4Num_    3 // both WaitforOrder_ and Waitfor4Num_
#define WaitforBulk_    4 // both WaitforString_ and WaitforBulk_
#define WaitforFile_    5
#define WaitforFourth_  6
#define WaitforThird_   7
#define WaitforSecond_  8
#define WaitforFirst_   9
#define WaitforEOT_     10

#define err_State_NotDetect                  10
#define err_SOH_NotDetect                    11
#define err_EOT_NotDetect                    12
#define err_Prefix_NotDetect                 13
#define err_Suffix_Bulk_NotDetect            14
#define err_Suffix_NumberEndPoint_NotDetect  15
#define err_Suffix_StringEndPoint_NotDetect  16
#define err_OutOfBulkSize                    17
#define err_OutOfStringSize                  18
#define err_IncompleteFile                   19
#define err_Order_NotDetect                  20
#define err_Time_Out                         100

// #define TotalCharAnyLinePrint_  (75 - 2)
// #define TotalLinesPrint_         26 // 50

// #define MaxOfPrintBulkSize_      TotalCharAnyLinePrint_ * TotalLinesPrint_        // MOPBS_ (73*50 = 3,650)    (73*26 = 1,898)

#define validTimeOut_	1000L

const int MRST = 5; // Hardware reset pin for PU

const int GWP = 16; // Hardware status pin to PU

bool RevisionMatch = false;

// WiFi and network status variables
U8 E_QuantitySSID = 0;
U16 E_PingResult = 0;
U32 E_RSSI;

U8 E_ShowHideWeight;
U32 E_IDIndex = 0;
U32 E_SerialNumber = UnDefinedNum_;
U8 E_AccessLevel = UnknownLevel_, Old_AccessLevel = UnknownLevel_;

C8 E_SystemLanguage = result_Undefined;
C8 E_MainVersion[FCSTS_ + 1] = {Null_}; // PU main version
C8 E_MessageStr[FCSTS_ + 1];
C8 E_FileName[FCSTS_ + 1];
C8 E_UserName[FCSTS_ + 4]; // +3 added, and 1 NULL terminator
bool IsExecutable = false;

// bool DateCompressType = Gregorian_;
U32 ResourceDateTime = 0;

// same as TimeStamp struct
U16 year = 0;
U8  month = 0;
U8  day = 0;
U8  hour = 0;
U8  minute = 0;
U8  second = 0;

void onDateTimeReceived();

bool IsGetPU_LCD = false, IsGetPU_Key = false;

U8 eSuffix, ePrefix;
U8 eProcessState = WaitforSOH_;
U8 eSubProcessState;

S32 eWiFiPack4Byte;
U8 ePart[4];

U8 eSendByte[4 + FCSTS_];
U8 CommandSize;

C8 PopDatStr[FCSTS_ + 4]; // +3 added, and 1 NULL terminator

S32 eDatNum[10];
C8 eDatStr[10][10];

bool InitializeUARTService();
U8 initStatus = 0;
U8 eOldMasterStatus = 0xff;
U8 LastESPStatus = 0xff;
U32 timeOut;

// End Point Status __________________________________________________________________________________

U8   eRequestedEP  = (U8)'?';
bool eProductReady = false;
C8*  eProductStr   = nullptr;

// Bulk Buffer Data __________________________________________________________________________________

C8*  BulkBuffer = NULL;
bool BulkDetected = false;
U32  CurrentDataSize = 0;
U32  FinalDataSize   = 0;
U16  BulkBufferSize  = 0;
U16  BulkReadCounter;
U16  BulkWriteCounter;
U16  BulkTotalRead;
U16  BulkTotalWrite;
bool BulkOverflow = false;
U32  dataTimeOut = 0;

// Top Level Ready ___________________________________________________________________________________

U8   eMasterStatus = Busy_;
bool eDateTimeReady = false;
bool ePowerReady = false;
bool eWeightReady = false;
bool eMultiSettingsSent = false;
bool eSendingWeigh = false;

U16 ePowerStatus, eBatteryValue;
S32 eWeightValue = UnDefinedNum_;

U8 currentNetStatus = 0, oldNetStatus = 0xff;

// Response Codes ------------------------------------------------------------------------------------

inline void ResponseCodeProcessor(U8 ePrefixIn, U8 eSuffixIn, U16 eResponseIn);
U16 eResponsePrefix, eResponseSuffix, eResponseCode;

// ESP Primary function ______________________________________________________________________________

void SendCommand(U8 PrefixIn, U8 SuffixIn, S32 N);
bool WaitForSerialProcessing();

U32 GetBaudRateIndex();
void SetBaudRateIndex(U32 baud);

// ESP top function __________________________________________________________________________________

U8   Request_GivePUStatus();
void SendESPStatus_ToPU(U8 status, bool force = false);
void SendResponseCode_ToPU(U8 Prefix, U8 Suffix, U8 Response);
void Request_Beep(U8 num);
bool Request_GiveBasicData();
U8   Request_IsWeightHidden();
bool Request_ShowHideWeight(U8 val);
S32  Request_GiveWeight();
void Request_SendingWeight(bool sending);
U32 Request_GiveReceipt(U32 ReceiptNo);
U32 Request_GiveReport(U16 dateFrom, U16 dateTo);
bool Request_GivePower();
bool Request_Tare();
bool Request_SendMessage_ToPU(const C8 *str, U8 title, U8 buttons, U8 icon);

// Endpoints Read & Write ____________________________________________________________________________

bool WriteEPtoPU_Num(U8 EndPoint);
bool WriteEPtoPU_Num(U8 EndPoint, U32 Value);
bool WriteEPtoPU_Str(U8 EndPoint, bool _wait = true);
bool WriteEPtoPU_DateTime();
bool ReadEPfromPU_Num(U8 EndPoint);
bool ReadEPfromPU_Str(U8 EndPoint);
bool ReadEPfromPU_DateTime();

// Send & Receive ____________________________________________________________________________________

void SerialPortReceiveProcess();
bool CanInterruptSerialProcessing(U8 echESP);
bool WaitForBulkDataComplete();
bool WaitForPU_Ready();
void SOH_Processor(U8 echESP);
void PrefixProcessor();
void x4NumProcessor(U8 echESP);
void BulkDataProcessor(U8 echESP);
void FileDataProcessor(U8 echESP);
void EOT_Processor(U8 echESP);
bool PushToNumEndPoint(U8 EndPoint, S32 n);
bool PushToStrEndPoint(U8 EndPoint, const C8 *str);
void PopFromStrEndPoint(U8 EndPoint);
S32  PopFromNumEndPoint(U8 EndPoint);
void OrderProcessor();
void FileOrderProcessor();
void DiagProcessor();
void ResetReceiveProcess(U8 result);

U32 Request_GiveFile(const C8* filename, U32 start, U32 length);

// Bulk Buffer _______________________________________________________________________________________

#define BulkIsStarted() (CurrentDataSize > 0 && BulkTotalWrite > 0)
#define BulkIsCompleted() (FinalDataSize > 0)

inline bool BulkIsBusy();

void BulkReset(U32 BulkSize);
S32 GetBulkRemain();
C8 ReadFromBulk();
bool WriteToBulk(C8 ch);
void WriteBulkTerminator();
U8 BulkWriteStringTo(C8* Str, U16 Size);

// void onBulkOverflow(C8 ch);
void onBulkDataReceived();

#define RemainingUARTBuffer() ((S32)Serial_Available() + GetBulkRemain())

// Debug Tools _______________________________________________________________________________________

#ifdef DebugTools
#ifndef DEBUG_SIZE
#define DEBUG_SIZE 2048
#endif
C8 DebugData[DEBUG_SIZE] = {Null_};
U16 DebugCounter = 0;
U8 DebugLastError = 0;
U8 DebugLastDirection = 0xff;
U8 inhibitDebugSend[2] = {0xff, 0xff};
void onBulkOverflow(C8 ch);
void WriteHexToDebug(U8 n);
void WriteToDebug(const String str);
void WriteToDebug(const C8* str);
void WriteToDebug(const C8* str, U8 padding, C8 ch);
void _ResetReceiveProcess(U8 result, U16 line);
void _ResetReceiveProcess(U8 result);
inline void ReceiveCommandDebug(U8 result, U16 line);
#define ResetReceiveProcess(x) _ResetReceiveProcess(x, __LINE__)
void WriteToDebugSerialReceive(C8 ch);
void WriteToDebugSerialTransmit(C8 ch);
void _LogMessageInFile(String message, const C8* file, U16 line);
#define LogMessageInFile(x) _LogMessageInFile(x, __FILE__, __LINE__)
void LogTimestamp();
#else
void _ResetReceiveProcess(U8 result);
#define ResetReceiveProcess(x) _ResetReceiveProcess(x)
#define LogMessageInFile(x) do {} while(0)
#endif

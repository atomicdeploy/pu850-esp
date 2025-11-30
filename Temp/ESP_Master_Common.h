// Common Define
//-----------------------------------------------------------------------------
//
#define ESP_ProtocolRevision_          2       //

// ID MessageIcons
#define ID_QuestionIcon_               1       //
#define ID_MemoryIcon_                 2       //
#define ID_ErrorIcon_                  3       //
#define ID_WiFiIcon_                   4       //
#define ID_CautionIcon_                5       //
#define ID_DeleteIcon_                 6       //
#define ID_InformationIcon_            7       //
#define ID_WaitIcon_                   8       //

// NetStatus Status Codes
#define NetStatus_UnDefined_           0       // Undefined Status
#define NetStatus_OK_                  1       // OK (Connected)
#define NetStatus_Disconnected_        2       // Disconnected
#define NetStatus_Connecting_          3       // Connecting...
#define NetStatus_NotFound_            4       // SSID Not Found (Unavailable)
#define NetStatus_WrongPassword_       5       // Wrong Password
#define NetStatus_ConnectFail_         6       // Connection Failed
#define NetStatus_Idle_                7       // Station is Idle

// Internet Status Codes
#define InternetStatus_UnDefined_      0       // Undefined Status
#define InternetStatus_Available_      1       // OK (Internet is Available)
#define InternetStatus_Error_          2       // Error, Internet Unavailable


//  Hex    Esc.   Name (1967)
//  0x0    \0     Null                             NUL
//  0x1           Start of Heading                 SOH
//  0x2           Start of Text                    STX
//  0x3           End of Text                      ETX
//  0x4           End of Transmission              EOT
//  0x5           Enquiry                          ENQ
//  0x6           Acknowledgement                  ACK
//  0x7    \a     Bell                             BEL
//  0x8    \b     Backspace[e][f]                  BS
//  0x9    \t     Horizontal Tab[g]                HT
//  0x0A   \n     Line Feed                        LF
//  0x0B   \v     Vertical Tab                     VT
//  0x0C   \f     Form Feed                        FF
//  0x0D   \r     Carriage Return[h]               CR
//  0x0E          Shift Out                        SO
//  0x0F          Shift In                         SI
//  0x10          Data Link Escape                 DLE
//  0x11          Device Control 1                 DC1
//  0x12          Device Control 2                 DC2
//  0x13          Device Control 3                 DC3
//  0x14          Device Control 4                 DC4
//  0x15          Negative Acknowledgement         NAK
//  0x16          Synchronous Idle                 SYN
//  0x17          End of Transmission Block        ETB
//  0x18          Cancel                           CAN
//  0x19          End of Medium                    EM
//  0x1A          Substitute                       SUB
//  0x1B   \e[i]  Escape[j]                        ESC
//  0x1C          File Separator                   FS
//  0x1D          Group Separator                  GS
//  0x1E          Record Separator                 RS
//  0x1F          Unit Separator                   US
//  0x7F          Delete[l][f]                     DEL

// Protocol
//-----------------------------------------------------------------------------
// List of ComOut(Commands Out) Master send to ESP 7

#define FCSTS_          15          // Fix Curent String Transfer Size
#define FBPS_           512         // Fix Bulk Packet Size

#define Graphic_ReportFormat_          0       //
#define HTML_File_ReportFormat_        1       //


#define SOH             0x01        // Start of Heading
#define EOT             0x04        // End of Transmission
#define SYN             0x16        // Synchronous Idle (synchronize or cancel curent transmit or operation)
#define ENQ             0x05        // Enquiry (continue file sending)


// Prefix
#define ESP_Prefix_GetNum               '<'        //
#define ESP_Prefix_SetNum               '>'        //
#define ESP_Prefix_GetStr               '{'        //
#define ESP_Prefix_SetStr               '}'        //
#define ESP_Prefix_Request              '['        //
#define ESP_Prefix_Order                ']'        //
#define ESP_Prefix_GetBulk              '('        //
#define ESP_Prefix_SendBulk             ')'        //
#define ESP_Prefix_FileRequest          '$'        //
#define ESP_Prefix_FileOrder            '@'        //
#define ESP_Prefix_RequestLCD           '~'        //
#define ESP_Prefix_OrderLCD             '!'        //
#define ESP_Prefix_DiagCmd              '#'        //
#define ESP_Prefix_ProgrammingCmd       '%'        //


// ESP Get/Set Number Suffix
#define ESP_Suffix_Add_Dat0              '0'       //

#define ESP_Suffix_Add_Dat9              '9'       //
#define ESP_Suffix_NetFlags              'a'       //
#define ESP_Suffix_NetStatus             'b'       //
#define ESP_Suffix_AppRunningCount       'c'       //
#define ESP_Suffix_InternetStatus        'd'       //
#define ESP_Suffix_DHCP_Status           'e'       //
#define ESP_Suffix_PublicIP              'f'       //
#define ESP_Suffix_AP_IP_Addr            'g'       //
#define ESP_Suffix_AP_SubnetMask         'h'       //
#define ESP_Suffix_AP_DevicesConnected   'i'       //
#define ESP_Suffix_AP_MaxDevices         'j'       //
#define ESP_Suffix_AP_MacOUI             'k'       //
#define ESP_Suffix_AP_MacNIC             'l'       //
#define ESP_Suffix_Station_IP_Addr       'm'       //
#define ESP_Suffix_Station_SubnetMask    'n'       //
#define ESP_Suffix_Station_GateWay       'o'       //
#define ESP_Suffix_Station_DNS_Addr1     'p'       //
#define ESP_Suffix_Station_MacOUI        'q'       //
#define ESP_Suffix_Station_MacNIC        'r'       //
#define ESP_Suffix_Station_RSSI          's'       //
#define ESP_Suffix_QuantitySSID          't'       //
#define ESP_Suffix_Ping                  'u'       //
#define ESP_Suffix_PortHTTP              'v'       //
#define ESP_Suffix_PortTelnet            'w'       //
#define ESP_Suffix_BaudRate              'x'       //
#define ESP_Suffix_MaxSessions           'y'       //
#define ESP_Suffix_Station_DNS_Addr2     'z'       //

#define ESP_Suffix_AccessLevel           'A'       //
#define ESP_Suffix_SerialNumber          'B'       //
#define ESP_Suffix_Revision              'C'       //
#define ESP_Suffix_HideWeigh             'D'       //
#define ESP_Suffix_IDIndex               'E'       //
#define ESP_Suffix_ReportMode            'F'       //
#define ESP_Suffix_IsExecutable          'G'       //


#define ESP_Suffix_KeyBoard              'J'       //
#define ESP_Suffix_FileSeekOffset	 'K'       //
#define ESP_Suffix_FileSeekLength	 'L'       //




// ESP Get/Set String Suffix
#define ESP_Suffix_DatStr0               '0'       //

#define ESP_Suffix_DatStr9               '9'       //
#define ESP_Suffix_StationSSID           'a'       //
#define ESP_Suffix_StationPass           'b'       //
#define ESP_Suffix_HostName              'c'       //
#define ESP_Suffix_AP_Pass               'd'       //
#define ESP_Suffix_AP_SSID               'e'       //
#define ESP_Suffix_MessageStr            'f'       //
#define ESP_Suffix_AccessPass            'g'       //
#define ESP_Suffix_UserName              'h'       //
#define ESP_Suffix_TransmitFileName      'i'       //
#define ESP_Suffix_MainVersion           'j'       //
#define ESP_Suffix_FirmwareHash          'k'       //

// ESP Request/Order Suffix
#define ESP_Suffix_Status                '0'       //
#define ESP_Suffix_Weigh                 '1'       //
#define ESP_Suffix_Power                 '2'       //
#define ESP_Suffix_Tare                  '3'       //

#define ESP_Suffix_SortRSSI              '5'       //
#define ESP_Suffix_Receipt               '6'       //
#define ESP_Suffix_WhatDateTime          '7'       //
#define ESP_Suffix_Bell                  '8'       //
#define ESP_Suffix_DateTimeInfo          '9'       //
#define ESP_Suffix_MessageBox            'a'       //
#define ESP_Suffix_BasicData             'b'       //
#define ESP_Suffix_Response              'c'       //
#define ESP_Suffix_ConfirmSettings       'd'       //
#define ESP_Suffix_Report                'e'       //
#define ESP_Suffix_SystemLanguage        'f'       //
#define ESP_Suffix_GoToChart             'g'       //
#define ESP_Suffix_MultiSettings         'h'       //
#define ESP_Suffix_SessionOrder          'i'       //
#define ESP_Suffix_RestoreBackup         'j'       //


// ESP Receive/Send Bulk Suffix
#define ESP_Suffix_Bulk_Receipt          'R'       //
#define ESP_Suffix_Bulk_Report           'P'       //


// ESP Receive/Send File Suffix
#define ESP_Suffix_ReceiveFile           'r'       //
#define ESP_Suffix_SendFile              's'       //
#define ESP_Suffix_SendFileSize	         'z'       //
#define ESP_Suffix_SendFileDateTime      't'       //
#define ESP_Suffix_SendFileMetadata	 'm'       //


// ESP RequestLCD/OrderLCD  Suffix
#define ESP_Suffix_MirrorLCD             'a'       //
#define ESP_Suffix_MonitorLCD            'b'       //
#define ESP_Suffix_setBlockLCD           'c'       //
#define ESP_Suffix_FillLCD               'd'       //
#define ESP_Suffix_ClearLCD              'e'       //


// Diag/Programmer Board Commands Suffix
#define Diag_Suffix_SetLED               'l'       //
#define Diag_Suffix_DataFlagBits         'd'       //
#define Diag_Suffix_Programming_ESP      'E'       //
#define Diag_Suffix_Programming_AVR      'V'       //
#define Diag_Suffix_ExitProgramming      'X'       //


#define ESP_Suffix_ProgramRequest        'P'       //

#define ESP_Suffix_Reboot                'R'       //

#define Group_None                       0x0       //
#define Group_Station                    0x1       //
#define Group_AP                         0x2       //
#define Group_Services                   0x3       //
#define Group_All                        0xff      //



//  Common Standard RFC Response Codes
//
//  +------+-------------------------------+
//  | Code | Reason-Phrase                 |
//  +------+-------------------------------+
//  | 100  | Continue                      |
//  | 101  | Switching Protocols           |
//  | 200  | OK                            |
//  | 201  | Created                       |
//  | 202  | Accepted                      |
//  | 203  | Non-Authoritative Information |
//  | 204  | No Content                    |
//  | 205  | Reset Content                 |
//  | 206  | Partial Content               |
//  | 300  | Multiple Choices              |
//  | 301  | Moved Permanently             |
//  | 302  | Found                         |
//  | 303  | See Other                     |
//  | 304  | Not Modified                  |
//  | 305  | Use Proxy                     |
//  | 307  | Temporary Redirect            |
//  | 400  | Bad Request                   |
//  | 401  | Unauthorized                  |
//  | 402  | Payment Required              |
//  | 403  | Forbidden                     |
//  | 404  | Not Found                     |
//  | 405  | Method Not Allowed            |
//  | 406  | Not Acceptable                |
//  | 407  | Proxy Authentication Required |
//  | 408  | Request Timeout               |
//  | 409  | Conflict                      |
//  | 410  | Gone                          |
//  | 411  | Length Required               |
//  | 412  | Precondition Failed           |
//  | 413  | Payload Too Large             |
//  | 414  | URI Too Long                  |
//  | 415  | Unsupported Media Type        |
//  | 416  | Range Not Satisfiable         |
//  | 417  | Expectation Failed            |
//  | 426  | Upgrade Required              |
//  | 500  | Internal Server Error         |
//  | 501  | Not Implemented               |
//  | 502  | Bad Gateway                   |
//  | 503  | Service Unavailable           |
//  | 504  | Gateway Timeout               |
//  | 505  | Version Not Supported         |
//  +------+-------------------------------+

// ESP Message Response Codes
#define result_Success                 0x0       // The request was understood, finished executing without any errors
#define result_Fail                    0x1       // The request was understood, attempted to execute, but failed to finish
#define result_Reject                  0x2       // The request is valid, however refusing to be executed
#define result_Processing              0x3       // The request is currently executing, requires time to finish
#define result_Not_Found               0x4       // The request was understood, however no matching results exist
#define result_Unauthorised            0x5       // The user does not have permission to execute the request
#define result_NotAcceptable           0x6       // The request cannot be executed in the current conditions
#define result_Cancelled               0x7       // The request Cancelled by user or abort by system
#define result_Creation_Error          0x8       // The request Cancelled by Creation_Error

#define result_Undefined               0xff      // Used when clearing the response code

// ------------------------------------------------------------

#define ReadyFor_GetData              0xC5       //
#define program_PocketSize            0x40       // 64 = 0x40 (must be define by hex)


#define ResultPack(Prefix,Suffix,Result)    (U32) ( (  ((U32)Prefix)<<24  ) | (  ((U32)Suffix)<<16  )   | (U32)Result )

// Serial Baud Rate Index
#define BR_1200      0x40       //
#define BR_2400      0xA0       //
#define BR_4800      0xCF       //
#define BR_9600      0x67       //
#define BR_14400     0x44       //
#define BR_19200     0x33       //
#define BR_38400     0x19       //
#define BR_56000     0x11       //
#define BR_57600     0x10       //
#define BR_115200    0x08       //

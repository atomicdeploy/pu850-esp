#define Ready_                  0
#define Busy_                   1
#define Booting_                2
#define Unknown_                0xff

#define FirstPart_              0
#define SecondPart_             1

#define Shamsi_                 0
#define Gregorian_              1

#define NotOK_			0
#define	Yes_			1
#define	OK_			1
#define	Cancel_			2
#define	No_			3
#define NotFound_               4
#define NotOK_Busy              5
#define ParallelOnly_           6
#define NotOK_Error_            7
#define CreationError_          8
#define WritingError_           9
#define TimeOutErrorResult_     10

#define All_                    1

#define Off_			0
#define On_			1
#define DotDot_                 2
#define Once_                   2

#define	Smal_			0
#define Equal_                  1
#define Large_                  2

#define False_			0
#define True_			1

#define hexFormat_              0
#define stringFormat_           1
#define bothFormat_             2

#define FullStep_               1
#define EmptyStep_              2

// Between Board Command
#define	OK_ReadCommand		0XC5
#define	OK_WriteCommand		0XA3

#define ReadADC_Command         0X00
#define ReadWeigh_Command	0X01
#define ReadCalib_Command	0X02
#define ReadOffset_Command      0X03
#define ReadCapacity_Command    0X04
#define ReadBalance_Command	0X05
#define Read4Parameter_Command	0X06
#define ReadWnAcc_Command	0x07
#define Read0Parameter_Command	0x08
#define ReadAccure_Command	0X09
#define ReadStandWeigh_Command	0X0A
#define ReadPanWeigh_Command	0X0B
#define ReadDecCap_Command	0X0C
#define ReadTest_Command	0X0D
#define ReadStatus_Command      0X0E
#define ReadADCVersion_Command	0X0F

#define	WriteDLC_Brand_Command  0X10

#define	WriteCalib_Command      0X12
#define WriteCapacity_Command   0X14
#define WriteBalance_Command	0X15
#define Write4Parameter_Command 0X16
#define WriteWnAcc_Command	0x17
#define Write0Parameter_Command 0x18
#define WriteAccure_Command	0X19
#define WriteStandWeigh_Command	0X1A
#define WritePanWeigh_Command	0X1B

#define	CalculCalib_Command     0X22
#define	CalculOffSet_Command	0X23

#define ReadDLC_Factor_Command    0X50
#define ReadDLC_Config_Command    0X51
#define ReadDLC_Ver_command       0X52
#define ReadDLC_Pack_command      0X53
#define ReadDLC_Fail_Command      0X54
#define ReadDLC_Offset_Command    0X55
#define ReadDebugData_Command     0X56

#define WriteDLC_Factor_Command       0X60
#define WriteDLC_Config_Command       0X61
#define WriteDLC_PackAdd_command      0X62
#define ClearDLC_OffSets_Command      0X63


#define	DLC_ResetCalib_Command        0X70
#define DLC_SearchQuestion_Command    0X71


#define MaxCapacity_    250000
#define MaxCapacityX10_ 2500000
#define MaxDLCCapX10_   500000

#define UnknownLoadCell_        0 // note : no change
#define LoadCellDigital_        1
#define LoadCellAnalog_         2
#define LoadCellKELI_           3
#define LoadCellFLINTEC_        4
#define LoadCellUtilCell_       5
#define LoadCellTransdutec_     6
#define LoadCellRiceLake_       7 // not complit


#define Status_Normaal_         0X00
#define Status_Finish_          0X5A
#define Status_Busy_            0x3C

#define FactorMagnify_  10000.0

#define minFactorDLC_    5000
#define maxFactorDLC_    15000
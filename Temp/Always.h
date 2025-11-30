#define        C8        char
#define        U8        unsigned char
#define        S8        signed char

#define        P8        unsigned char *
#define        P16       unsigned int *
#define        PS16      signed int *
#define        PS32      signed long int *
#define        P32       unsigned long int *

#define        C16       int
#define        U16       unsigned int
#define        S16       signed int
#define        U32       unsigned long int
#define        S32       signed long int


#define        F8        unsigned char flash
#define        PF8       unsigned char flash *
#define        PF16      unsigned int flash *
#define        CF8       char flash *

#define        BOOL      C8

#ifndef __CODEVISIONAVR__
#undef         BOOL
#define        BOOL      bool
#endif

#define       ErrorNum_         0xffffffff
#define       UnDefinedNum_     0x7fffffff
#define       InfinityU16_      0xffff

#define High_0                |= 0X01
#define High_1                |= 0X02
#define High_2                |= 0X04
#define High_3                |= 0X08
#define High_4                |= 0X10
#define High_5                |= 0X20
#define High_6                |= 0X40
#define High_7                |= 0X80

#define Low_0                &=~ 0X01
#define Low_1                &=~ 0X02
#define Low_2                &=~ 0X04
#define Low_3                &=~ 0X08
#define Low_4                &=~ 0X10
#define Low_5                &=~ 0X20
#define Low_6                &=~ 0X40
#define Low_7                &=~ 0X80

#define IsHigh_0        & 0X01) !=0x00)
#define IsHigh_1        & 0X02) !=0x00)
#define IsHigh_2        & 0X04) !=0x00)
#define IsHigh_3        & 0X08) !=0x00)
#define IsHigh_4        & 0X10) !=0x00)
#define IsHigh_5        & 0X20) !=0x00)
#define IsHigh_6        & 0X40) !=0x00)
#define IsHigh_7        & 0X80) !=0x00)

#define IsLow_0                & 0X01) ==0x00)
#define IsLow_1                & 0X02) ==0x00)
#define IsLow_2                & 0X04) ==0x00)
#define IsLow_3                & 0X08) ==0x00)
#define IsLow_4                & 0X10) ==0x00)
#define IsLow_5                & 0X20) ==0x00)
#define IsLow_6                & 0X40) ==0x00)
#define IsLow_7                & 0X80) ==0x00)

#define LowByte(U16)            ((U8)U16)
#define HighByte(U16)           ((U8)(U16>>8))
#define SetBit(adr,bit)         (adr |= (1<<bit))
#define ClearBit(adr,bit)       (adr &= ~(1<<bit))
#define BitIsSet(adr,bit)       (adr & (1<<bit))
#define BitIsClear(adr,bit)     (!( adr & (1<<bit)))


#define FirstByte(U32)          ((U8)U32)
#define SecondByte(U32)         ((U8)(U32>>8))
#define ThirdByte(U32)          ((U8)(U32>>16))
#define FourthByte(U32)         ((U8)(U32>>24))

#define FirstWord(U32)          ((U16)U32)
#define SecondWord(U32)         ((U16)(U32>>16))

#define PackU16(H,L)            ( (U16)H <<8 | (U16)L )

#define PackU32(I3,I2,I1,I0)    ( ((U32)I3<<24) | ((U32)I2<<16) | ((U32)I1<<8) | (U32)I0 )

#define ReverseOrder(B)         PackU32( FirstByte(B), SecondByte(B), ThirdByte(B), FourthByte(B) )

#define Null_                   '\0'        // Null character

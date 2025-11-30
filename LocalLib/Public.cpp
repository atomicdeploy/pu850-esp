#pragma once

#include "../ASA0002E.h"
#include "../defines.h"

inline void UnPack32(S32 N)
{
	ePart[0] = (U8)(N >> 24);
	ePart[1] = (U8)(N >> 16);
	ePart[2] = (U8)(N >> 8);
	ePart[3] = (U8)(N);
}

// CalendarType = Gregorian_;			// Forced Gregorian (ESP v2)

void eExpandDateTime(U16 CompressedDate, U16 CompressedTime, bool CalendarType = Gregorian_)
{
	year = CompressedDate >> 9;

	if (CalendarType) year += 2000;		// Year: 2000~2127  ->  0~127
	else              year += 1380; 	// Year: 1380~1507

	month = (CompressedDate >> 5) & 0X0F;
	day   = (CompressedDate)      & 0X1F;

	if (month == 0) year = 0;

	hour   = (CompressedTime >> 11) & 0X1F;
	minute = (CompressedTime >>  5) & 0X3F;
	second = (CompressedTime & 0X1F) << 1;
}

S32  eCompressDateTime(bool CalendarType = Gregorian_)
{
	U16 CompressedDate, CompressedTime;

	U16 _year = year;

	if (CalendarType) _year -= 2000;	// Year: 2000~2127  ->  0~127
	else              _year -= 1380;	// Year: 1380~1507

	CompressedDate = (_year << 9) | (month << 5)  | (day);
	CompressedTime = (hour << 11) | (minute << 5) | (second >> 1);

	return ((S32)CompressedDate << 16) | CompressedTime;  // as CompressedDateTime
}

#define CompressDate(Year, Month, Day) (U32)((((U16)Year) << 9) | (Month << 5) | (Day))

#define ShamsiU16toU8(y)    (U8)(y - 1380)
#define GregorianU16toU8(y) (U8)(y - 2000)

/*=====================================================================
TimeStamp.cpp
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at 2013-04-16 21:36:01 +0100
=====================================================================*/
#include "TimeStamp.h"


#include <Exception.h>
#include <Clock.h>
#include <InStream.h>
#include <OutStream.h>
#include <Platform.h>
#include <StringUtils.h>
#include <IncludeWindows.h>
#include <cassert>
#if !defined(_WIN32)
#include <sys/time.h>
#endif


TimeStamp::TimeStamp()
{

}


TimeStamp::~TimeStamp()
{

}


// day_of_week = days since Sunday - [0,6]
static const std::string dayAsString(int day_of_week)
{
	switch(day_of_week)
	{
	case 0:
		return "Sun";
	case 1:
		return "Mon";
	case 2:
		return "Tue";
	case 3:
		return "Wed";
	case 4:
		return "Thu";
	case 5:
		return "Fri";
	case 6:
		return "Sat";
	default:
		assert(0);
		return "";
	};
}


static const std::string monthString(int month)
{
	std::string monthstr;
	switch(month)
	{
	case 0:
		monthstr = "Jan";
		break;
	case 1:
		monthstr = "Feb";
		break;
	case 2:
		monthstr = "Mar";
		break;
	case 3:
		monthstr = "Apr";
		break;
	case 4:
		monthstr = "May";
		break;
	case 5:
		monthstr = "Jun";
		break;
	case 6:
		monthstr = "Jul";
		break;
	case 7:
		monthstr = "Aug";
		break;
	case 8:
		monthstr = "Sep";
		break;
	case 9:
		monthstr = "Oct";
		break;
	case 10:
		monthstr = "Nov";
		break;
	case 11:
		monthstr = "Dec";
		break;
	default:
		assert(0);
		break;
	};
	return monthstr;
}



const std::string TimeStamp::dayString() const
{
	time_t t = this->time;
	tm thetime;

	//const tm* thetime = localtime(&t);

	// Use threadsafe versions of localtime: 
#ifdef _WIN32
	localtime_s(&thetime, &t);
#else
	localtime_r(&t, &thetime);
#endif
	

	const int day = thetime.tm_mday; // Day of month (1 – 31).
	const int month = thetime.tm_mon; // Month (0 – 11; January = 0).
	const int year = thetime.tm_year + 1900; // tm_year = Year (current year minus 1900).

	return toString(day) + " " + monthString(month) + " " + toString(year);
}


static const std::string twoDigitString(int x)
{
	return ::leftPad(::toString(x), '0', 2);
}


const std::string TimeStamp::RFC822FormatedString() const // http://www.faqs.org/rfcs/rfc822.html
{
	time_t t = this->time;
	tm thetime;

	// Use threadsafe versions of localtime: 
#ifdef _WIN32
	localtime_s(&thetime, &t);
#else
	localtime_r(&t, &thetime);
#endif
	
	const int day_of_week = thetime.tm_wday; // days since Sunday - [0,6]
	const int day = thetime.tm_mday; // Day of month (1 – 31).
	const int month = thetime.tm_mon; // Month (0 – 11; January = 0).
	const int year = thetime.tm_year + 1900; // tm_year = Year (current year minus 1900).

	return dayAsString(day_of_week) + ", " + toString(day) + " " + monthString(month) + " " + toString(year) + " " + 
		twoDigitString(thetime.tm_hour) + ":" + twoDigitString(thetime.tm_min) + ":" + twoDigitString(thetime.tm_sec) + " GMT";
}


TimeStamp TimeStamp::currentTime()
{
	TimeStamp t;
	t.time = (uint64)Clock::getSecsSince1970();
	return t;
}


static const unsigned int TIMESTAMP_SERIALISATION_VERSION = 1;


void TimeStamp::writeToStream(OutStream& s) const
{
	s.writeUInt32(TIMESTAMP_SERIALISATION_VERSION);
	//s.writeUInt64(time);
	s.writeData(&time, sizeof(time));
}


void TimeStamp::readFromStream(InStream& stream)
{
	const uint32 v = stream.readUInt32();
	if(v != TIMESTAMP_SERIALISATION_VERSION)
		throw Indigo::Exception("Unhandled version " + toString(v) + ", expected " + toString(TIMESTAMP_SERIALISATION_VERSION) + ".");

	time = stream.readUInt64();
}


const std::string TimeStamp::timeAgoDescription() const // Returns a string like '1 hour ago'
{
	int diff_s = (int)(currentTime().time - this->time); // Get num seconds ago
	
	if(diff_s < 3600) // If less than 1 hour ago:
	{
		int diff_m = diff_s / 60;
		return diff_m == 1 ? "1 minute ago" : toString(diff_m) + " minutes ago";
	}
	else if(diff_s < (3600 * 24)) // Else if less than 1 day ago:
	{
		int diff_h = diff_s / 3600;
		return diff_h == 1 ? "1 hour ago" : toString(diff_h) + " hours ago";
	}
	else
	{
		int diff_d = diff_s / (3600 * 24);
		return diff_d == 1 ? "1 day ago" : toString(diff_d) + " days ago";
	}
}

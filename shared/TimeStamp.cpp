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


TimeStamp::TimeStamp() : time(0)
{

}


TimeStamp::TimeStamp(uint64 time_) : time(time_) {}


TimeStamp::~TimeStamp()
{

}


TimeStamp TimeStamp::fromComponents(int year, int month, int day, int hour, int minutes, int seconds)
{
	tm t;
	t.tm_year = year - 1900; // tm_year = years since 1900
	t.tm_mon = month; // months since January
	t.tm_mday = day; // day of the month ([1, 31])
	t.tm_hour = hour;
	t.tm_min = minutes;
	t.tm_sec = seconds;

#ifdef _WIN32
	const time_t res = _mkgmtime(&t);
#else
	const time_t res = timegm(&t);
#endif
	if(res == -1)
		throw glare::Exception("TimeStamp::fromComponents(): Failed to convert to time_t");
	return TimeStamp(res);
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


const std::string TimeStamp::dayAndTimeStringUTC() const
{
	time_t t = this->time;
	tm thetime;

	// Use threadsafe versions of localtime: 
#ifdef _WIN32
	gmtime_s(&thetime, &t);
#else
	gmtime_r(&t, &thetime);
#endif

	const int day = thetime.tm_mday; // Day of month (1 – 31).
	const int month = thetime.tm_mon; // Month (0 – 11; January = 0).
	const int year = thetime.tm_year + 1900; // tm_year = Year (current year minus 1900).

	return toString(day) + " " + monthString(month) + " " + toString(year) + ", " + ::leftPad(toString(thetime.tm_hour), '0', 2) + ":" + ::leftPad(toString(thetime.tm_min), '0', 2);
}


const std::string TimeStamp::RFC822FormatedString() const // http://www.faqs.org/rfcs/rfc822.html
{
	time_t t = this->time;

	try
	{
		return Clock::RFC822FormatedString(t);
	}
	catch(glare::Exception& )
	{
		return "[String formatting failed]";
	}
}


static const std::string twoDigitString(int x)
{
	return ::leftPad(::toString(x), '0', 2);
}


// See https://developer.mozilla.org/en-US/docs/Web/HTML/Date_and_time_formats#local_date_and_time_strings
// Returns a string like "1970-07-20T22:00"
const std::string TimeStamp::HTTPDateTimeFormattedStringUTC() const
{
	time_t t = this->time;

	tm thetime;
	// Get calender time in UTC.  Use threadsafe versions of gmtime.
#ifdef _WIN32
	const errno_t res = gmtime_s(&thetime, &t);
	if(res != 0)
		throw glare::Exception("time formatting failed");
#else
	const tm* res = gmtime_r(&t, &thetime);
	if(res == NULL)
		throw glare::Exception("time formatting failed");
#endif

	const int year = thetime.tm_year + 1900; // tm_year = Year (current year minus 1900).
	const int month = thetime.tm_mon; // Month (0 – 11; January = 0).
	const int day = thetime.tm_mday; // Day of month (1 – 31).

	// NOTE: not including optional seconds.  
	return toString(year) + "-" + twoDigitString(month + 1) + "-" + twoDigitString(day) + "T" + twoDigitString(thetime.tm_hour) + ":" + twoDigitString(thetime.tm_min);
}


std::string TimeStamp::durationDescription(int seconds)
{
	// If less than 1 minute, show just seconds
	if(seconds < 60)
		return toString(seconds) + " seconds";

	int mins = seconds / 60;

	const int days = mins / (24 * 60);
	mins -= days * 24 * 60;

	const int hours = mins / 60;
	mins -= hours * 60;

	std::string s;
	if(days == 1)
		s += "1 day";
	else if(days > 1)
		s += toString(days) + " days";
	if(hours > 0)
	{
		if(!s.empty())
			s += ", ";
		if(hours == 1)
			s += "1 hour";
		else
			s += ::toString(hours) + " hours";
	}
	if((mins > 0) || s.empty())
	{
		if(!s.empty())
			s += " and ";
		s += ::toString(mins) + ((mins > 1) ? " minutes" : " minute");
	}

	return s;
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
		throw glare::Exception("Unhandled version " + toString(v) + ", expected " + toString(TIMESTAMP_SERIALISATION_VERSION) + ".");

	time = stream.readUInt64();
}


int64 TimeStamp::numSecondsAgo() const
{
	return (int64)currentTime().time - (int64)this->time;
}


std::string TimeStamp::timeInFutureDescription(int64 time_minus_current_time)
{
	return "in " + durationDescription((int)time_minus_current_time);
}


std::string TimeStamp::timeDescription(int64 time_minus_current_time)
{
	if(time_minus_current_time < 0)
		return timeAgoDescription(/*seconds ago=*/-time_minus_current_time);
	else
		return timeInFutureDescription(time_minus_current_time);
}


std::string TimeStamp::timeAgoDescription(int64 seconds_ago)
{
	const int64 diff_s = seconds_ago;

	if(diff_s < 60) // If less than 1 minute ago:
	{
		return diff_s == 1 ? "1 second ago" : toString(diff_s) + " seconds ago";
	}
	else if(diff_s < 3600) // If less than 1 hour ago:
	{
		int64 diff_m = diff_s / 60;
		return diff_m == 1 ? "1 minute ago" : toString(diff_m) + " minutes ago";
	}
	else if(diff_s < (3600 * 24)) // Else if less than 1 day ago:
	{
		int64 diff_h = diff_s / 3600;
		return diff_h == 1 ? "1 hour ago" : toString(diff_h) + " hours ago";
	}
	else
	{
		int64 diff_d = diff_s / (3600 * 24);
		return diff_d == 1 ? "1 day ago" : toString(diff_d) + " days ago";
	}
}


std::string TimeStamp::timeAgoDescription() const // Returns a string like '1 hour ago'
{
	const int64 seconds_ago = (int64)currentTime().time - (int64)this->time; // Get num seconds ago
	return timeAgoDescription(seconds_ago);
}


std::string TimeStamp::timeDescription() const // Returns a string like '1 hour ago' or 'in 5 minutes'
{
	const int64 diff_s = (int64)this->time - (int64)currentTime().time;
	return timeDescription(diff_s);
}


#if BUILD_TESTS


#include <utils/TestUtils.h>


void TimeStamp::test()
{
	//-------------------------------- Test durationDescription --------------------------------
	testAssert(durationDescription(3600 * 24 * 7 + 4 * 3600 + 37 * 60 + 52) == "7 days, 4 hours and 37 minutes");
	testAssert(durationDescription(4 * 3600 + 37 * 60 + 52) == "4 hours and 37 minutes");
	testAssert(durationDescription(37 * 60 + 52) == "37 minutes");
	testAssert(durationDescription(52) == "52 seconds");
	testAssert(durationDescription(0) == "0 seconds");

	// Test singular day
	testAssert(durationDescription(3600 * 24) == "1 day");
	testAssert(durationDescription(3600 * 24 + 40 * 60) == "1 day and 40 minutes");

	// Test singular hour
	testAssert(durationDescription(3600) == "1 hour");
	testAssert(durationDescription(3600 + 40 * 60) == "1 hour and 40 minutes");

	// Test singular minute
	testAssert(durationDescription(60) == "1 minute");
	testAssert(durationDescription(60 + 10) == "1 minute");

	testAssert(durationDescription(4 * 3600) == "4 hours"); // shouldn't say "0 minutes" if there is an hour present
	
	//-------------------------------- Test timeInFutureDescription --------------------------------
	testAssert(timeInFutureDescription(3600 * 24 * 7 + 4 * 3600 + 37 * 60 + 52) == "in 7 days, 4 hours and 37 minutes");

	testAssert(timeInFutureDescription(60) == "in 1 minute");

	testAssert(timeInFutureDescription(40) == "in 40 seconds");

	testAssert(timeInFutureDescription(0) == "in 0 seconds");

	testAssert(timeInFutureDescription(-10) == "in -10 seconds");

	//-------------------------------- Test timeAgoDescription --------------------------------
	testAssert(timeAgoDescription(3600 * 24 * 7 + 4 * 3600 + 37 * 60 + 52) == "7 days ago");
	testAssert(timeAgoDescription(3600 * 24 * 1 + 4 * 3600 + 37 * 60 + 52) == "1 day ago");
	testAssert(timeAgoDescription(4 * 3600 + 37 * 60 + 52) == "4 hours ago");
	testAssert(timeAgoDescription(1 * 3600 + 37 * 60 + 52) == "1 hour ago");

	testAssert(timeAgoDescription(37 * 60 + 52) == "37 minutes ago");
	testAssert(timeAgoDescription(1 * 60 + 52) == "1 minute ago");

	testAssert(timeAgoDescription(52) == "52 seconds ago");
	testAssert(timeAgoDescription(1) == "1 second ago");

	testAssert(timeAgoDescription(-10) == "-10 seconds ago");

	//-------------------------------- Test timeDescription --------------------------------
	testAssert(timeDescription(3600 * 24 * 7 + 4 * 3600 + 37 * 60 + 52) == "in 7 days, 4 hours and 37 minutes");
	testAssert(timeDescription(37 * 60 + 52) == "in 37 minutes");
	testAssert(timeDescription(52) == "in 52 seconds");
	
	testAssert(timeDescription(-(37 * 60 + 52)) == "37 minutes ago");
	testAssert(timeDescription(-52) == "52 seconds ago");
}


#endif

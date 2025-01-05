/*=====================================================================
TimeStamp.h
-----------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include <Platform.h>
#include <string>
class OutStream;
class InStream;


/*=====================================================================
TimeStamp
---------

=====================================================================*/
class TimeStamp
{
public:
	TimeStamp();
	explicit TimeStamp(uint64 time);
	~TimeStamp();

	static TimeStamp fromComponents(
		int year, 
		int month, // months since January
		int day, // day of the month ([1, 31])
		int hour, 
		int minutes,
		int seconds
	);

	const std::string dayString() const; // E.g. "4 Jan 2025"
	const std::string dayAndTimeStringUTC() const; // E.g. "4 Jan 2025, 14:20"

	void writeToStream(OutStream& s) const;
	void readFromStream(InStream& s);

	static TimeStamp currentTime();

	int64 numSecondsAgo() const;

	const std::string timeAgoDescription() const; // Returns a string like '1 hour ago'

	const std::string timeDescription() const; // Returns a string like '1 hour ago' or 'in 5 minutes'

	const std::string RFC822FormatedString() const; // http://www.faqs.org/rfcs/rfc822.html

	const std::string HTTPDateTimeFormattedStringUTC() const; // e.g. 

	static const std::string durationDescription(int seconds); // E.g. "2 days, 3 hours and 30 minutes"

	bool operator <= (const TimeStamp& other) const { return time <= other.time; }
	bool operator >= (const TimeStamp& other) const { return time >= other.time; }

	uint64 time; // Seconds since 1970 UTC.
};



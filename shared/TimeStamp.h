/*=====================================================================
TimeStamp.h
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at 2013-04-16 21:36:01 +0100
=====================================================================*/
#pragma once


#include <Platform.h>
#include <string>
class OutStream;
class InStream;


/*=====================================================================
TimeStamp
-------------------

=====================================================================*/
class TimeStamp
{
public:
	TimeStamp();
	~TimeStamp();


	const std::string dayString() const;

	void writeToStream(OutStream& s) const;
	void readFromStream(InStream& s);

	static TimeStamp currentTime();

	const std::string timeAgoDescription() const; // Returns a string like '1 hour ago'

	const std::string RFC822FormatedString() const; // http://www.faqs.org/rfcs/rfc822.html

	uint64 time;
};

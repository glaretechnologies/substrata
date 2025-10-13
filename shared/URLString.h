/*=====================================================================
URLString.h
-----------

=====================================================================*/
#pragma once


#include <utils/STLArenaAllocator.h>
#include <string>


typedef std::basic_string<char, std::char_traits<char>, glare::STLArenaAllocator<char>> URLString;


inline std::string toStdString(const URLString& s) { return std::string(s.begin(), s.end()); }
inline URLString toURLString(const std::string& s) { return URLString(s.begin(), s.end()); }


struct URLStringHasher
{
	size_t operator () (const URLString& s) const
	{
		std::hash<std::string_view> h;
		return h(s);
	}
};

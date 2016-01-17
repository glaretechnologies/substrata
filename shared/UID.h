/*=====================================================================
UID.h
----------------
File created by ClassTemplate on Thu May 05 01:07:24 2005
Code By Nicholas Chapman.
=====================================================================*/
#pragma once


#include <Platform.h>
#include <OutStream.h>
#include <InStream.h>
#include <limits>
#include <string>



class UID
{
public:
	UID() : v(invalidUID().value()) {}
	UID(uint64 v_) : v(v_) {}

	static UID invalidUID() { return std::numeric_limits<uint64>::max(); }

	uint64 value() const { return v; }

	bool valid() const { return v != invalidUID().value(); }

	const std::string toString() const;

	bool operator == (const UID& other) const { return v == other.v; }
	bool operator != (const UID& other) const { return v != other.v; }

	bool operator < (const UID& other) const { return v < other.v; }
	bool operator >= (const UID& other) const { return v >= other.v; }

//private:
	uint64 v;
};


inline void writeToStream(const UID& uid, OutStream& stream)
{
	stream.writeData(&uid.v, sizeof(uid.v));
}


inline UID readUIDFromStream(InStream& stream)
{
	UID uid;
	stream.readData(&uid.v, sizeof(uid.v));
	return uid;
}

/*=====================================================================
ParcelID.h
----------
Copyright Glare Technologies Limited 2018 -
=====================================================================*/
#pragma once


#include <Platform.h>
#include <OutStream.h>
#include <InStream.h>
#include <StringUtils.h>
#include <limits>
#include <string>


/*=====================================================================
ParcelID
--------
=====================================================================*/
class ParcelID
{
public:
	ParcelID() : v(invalidParcelID().value()) {}
	explicit ParcelID(uint32 v_) : v(v_) {}

	static ParcelID invalidParcelID() { return ParcelID(std::numeric_limits<uint32>::max()); }

	uint32 value() const { return v; }

	bool valid() const { return v != invalidParcelID().value(); }

	const std::string toString() const { return ::toString(v); }

	bool operator == (const ParcelID& other) const { return v == other.v; }
	bool operator != (const ParcelID& other) const { return v != other.v; }

	bool operator < (const ParcelID& other) const { return v < other.v; }
	bool operator >= (const ParcelID& other) const { return v >= other.v; }

	uint32 v;
};


inline void writeToStream(const ParcelID& uid, OutStream& stream)
{
	stream.writeData(&uid.v, sizeof(uid.v));
}


[[nodiscard]] inline ParcelID readParcelIDFromStream(InStream& stream)
{
	ParcelID uid;
	stream.readData(&uid.v, sizeof(uid.v));
	return uid;
}

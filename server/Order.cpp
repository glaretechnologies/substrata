/*=====================================================================
Order.cpp
---------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "Order.h"


#include <Exception.h>
#include <StringUtils.h>


Order::Order()
{}


Order::~Order()
{}


static const uint32 ORDER_SERIALISATION_VERSION = 1;


void writeToStream(const Order& order, OutStream& stream)
{
	// Write version
	stream.writeUInt32(ORDER_SERIALISATION_VERSION);

	stream.writeData(&order.id, sizeof(order.id));
	
	writeToStream(order.user_id, stream);

	writeToStream(order.parcel_id, stream);

	order.created_time.writeToStream(stream);

	stream.writeStringLengthFirst(order.payer_email);
	stream.writeDouble(order.gross_payment);
	stream.writeStringLengthFirst(order.currency);

	stream.writeStringLengthFirst(order.paypal_data);

	stream.writeInt32(order.confirmed ? 1 : 0);
}


void readFromStream(InStream& stream, Order& order)
{
	// Read version
	const uint32 v = stream.readUInt32();
	if(v > ORDER_SERIALISATION_VERSION)
		throw glare::Exception("Unsupported version " + toString(v) + ", expected " + toString(ORDER_SERIALISATION_VERSION) + ".");

	order.id = stream.readUInt64();

	order.user_id = readUserIDFromStream(stream);

	order.parcel_id = readParcelIDFromStream(stream);

	order.created_time.readFromStream(stream);

	order.payer_email = stream.readStringLengthFirst(10000);
	order.gross_payment = stream.readDouble();
	order.currency = stream.readStringLengthFirst(10000);

	order.paypal_data = stream.readStringLengthFirst(10000);

	order.confirmed = stream.readInt32() != 0;
}

/*=====================================================================
Order.h
-------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#pragma once


#include "../shared/TimeStamp.h"
#include "../shared/UserID.h"
#include "../shared/ParcelID.h"
#include <ThreadSafeRefCounted.h>
#include <Reference.h>
#include <OutStream.h>
#include <InStream.h>


/*=====================================================================
Order
-----
Order for a parcel - payment amount etc..
=====================================================================*/
class Order : public ThreadSafeRefCounted
{
public:
	Order();
	~Order();


	uint64 id;

	UserID user_id; // User who made the order

	ParcelID parcel_id; // Parcel that was purchased

	TimeStamp created_time;

	std::string payer_email; // Customer's primary email address (from PayPal purchase)
	double gross_payment; // Full amount of the customer's payment, before transaction fee is subtracted (from PayPal purchase)
	std::string currency; // For payment IPN notifications, this is the currency of the payment. (from PayPal purchase)

	std::string paypal_data; // All paypal data

	bool confirmed; // Has payment been confirmed?
};


typedef Reference<Order> OrderRef;


void writeToStream(const Order& order, OutStream& stream);
void readFromStream(InStream& stream, Order& order);

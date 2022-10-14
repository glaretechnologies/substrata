/*=====================================================================
parcel.ts
---------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/


import { BufferIn, readUInt32, readUInt64, readFloat, readDouble, readStringFromStream } from './bufferin.js';
import { BufferOut } from './bufferout.js';
import {
	Vec2d, Vec3f, Vec3d, readVec2dFromStream, readVec3fFromStream, readVec3dFromStream, Colour3f, Matrix2f, readColour3fFromStream, readMatrix2fFromStream,
	readUserIDFromStream, readTimeStampFromStream, readUIDFromStream, readParcelIDFromStream, writeUID
} from './types.js';


export class Parcel {
	parcel_id: number;
	owner_id: number;
	created_time: bigint;
	description: string;
	admin_ids: Array<Number>;
	writer_ids: Array<Number>;
	child_parcel_ids: Array<Number>;
	all_writeable: boolean;
	verts: Array<Vec2d>;
	zbounds: Vec2d;
	flags: number;
	parcel_auction_ids: Array<Number>;
	spawn_point: Vec3d;
	owner_name: string;
	admin_names: Array<string>;
	writer_names: Array<string>;
}


export function readParcelFromNetworkStreamGivenID(buffer_in: BufferIn) {
	let parcel = new Parcel();

	parcel.owner_id = readUserIDFromStream(buffer_in);
	parcel.created_time = readTimeStampFromStream(buffer_in);
	parcel.description = readStringFromStream(buffer_in);

	//console.log("parcel: ", parcel)
	//console.log("parcel.description: ", parcel.description)

	// Read admin_ids
	{
		let num = readUInt32(buffer_in);
		if (num > 100000)
			throw "Too many admin_ids: " + num.toString();
		parcel.admin_ids = [];
		for (let i = 0; i < num; ++i)
			parcel.admin_ids.push(readUserIDFromStream(buffer_in));

		//console.log("parcel.admin_ids: ", parcel.admin_ids)
	}

	// Read writer_ids
	{
		let num = readUInt32(buffer_in);
		if (num > 100000)
			throw "Too many writer_ids: " + num.toString();
		parcel.writer_ids = [];
		for (let i = 0; i < num; ++i)
			parcel.writer_ids.push(readUserIDFromStream(buffer_in));
	}

	// Read child_parcel_ids
	{
		let num = readUInt32(buffer_in);
		if (num > 100000)
			throw "Too many child_parcel_ids: " + num.toString();
		parcel.child_parcel_ids = [];
		for (let i = 0; i < num; ++i)
			parcel.child_parcel_ids.push(readUserIDFromStream(buffer_in));
	}

	// Read all_writeable
	{
		let val = readUInt32(buffer_in);
		if (val != 0 && val != 1)
			throw "Invalid boolean value";
		parcel.all_writeable = val != 0;
	}

	parcel.verts = []
	for (let i = 0; i < 4; ++i) {
		parcel.verts.push(readVec2dFromStream(buffer_in));

		//console.log("parcel.verts[i]: ", parcel.verts[i]);
	}

	parcel.zbounds = readVec2dFromStream(buffer_in);

	{
		// Read parcel_auction_ids
		let num = readUInt32(buffer_in);
		if (num > 100000)
			throw "Too many parcel_auction_ids: " + num.toString();
		parcel.parcel_auction_ids = []
		for (let i = 0; i < num; ++i)
			parcel.parcel_auction_ids.push(readUInt32(buffer_in));
	}

	// Read flags
	parcel.flags = readUInt32(buffer_in)

	// Read spawn_point
	parcel.spawn_point = readVec3dFromStream(buffer_in);

	//console.log("parcel.parcel_auction_ids: ", parcel.parcel_auction_ids)

	parcel.owner_name = readStringFromStream(buffer_in);

	//console.log("parcel.owner_name: ", parcel.owner_name)

	// Read admin_names
	{
		let num = readUInt32(buffer_in);
		if (num > 100000)
			throw "Too many admin_names: " + num.toString();
		parcel.admin_names = []
		for (let i = 0; i < num; ++i)
			parcel.admin_names.push(readStringFromStream(buffer_in));
	}

	// Read writer_names
	{
		let num = readUInt32(buffer_in);
		if (num > 100000)
			throw "Too many writer_names: " + num.toString();
		parcel.writer_names = []
		for (let i = 0; i < num; ++i)
			parcel.writer_names.push(readStringFromStream(buffer_in));
	}

	//console.log("parcel: ", parcel)

	return parcel;
}

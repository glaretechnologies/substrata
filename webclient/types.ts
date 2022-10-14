/*=====================================================================
types.ts
--------
Copyright Glare Technologies Limited 2022 -

Miscellaneous types and read/write functions for them.
=====================================================================*/


import { BufferIn, readUInt32, readUInt64, readFloat, readDouble, readStringFromStream } from './bufferin.js';
import { BufferOut } from './bufferout.js';


export function readUIDFromStream(buffer_in: BufferIn): bigint {
	return readUInt64(buffer_in);
}


export function writeUID(buffer_out: BufferOut, uid: bigint) {
	buffer_out.writeUInt64(uid);
}


export class Vec2d {
	x: number;
	y: number;

	constructor(x_, y_) {
		this.x = x_;
		this.y = y_;
	}
}

export class Vec3f {
	x: number;
	y: number;
	z: number;

	constructor(x_, y_, z_) {
		this.x = x_;
		this.y = y_;
		this.z = z_;
	}

	writeToStream(buffer_out: BufferOut) {
		buffer_out.writeFloat(this.x);
		buffer_out.writeFloat(this.y);
		buffer_out.writeFloat(this.z);
	}
}

export class Matrix2f {
	x: number;
	y: number;
	z: number;
	w: number;

	constructor(x_, y_, z_, w_) {
		this.x = x_;
		this.y = y_;
		this.z = z_;
		this.w = w_;
	}

	writeToStream(buffer_out: BufferOut) {
		buffer_out.writeFloat(this.x);
		buffer_out.writeFloat(this.y);
		buffer_out.writeFloat(this.z);
		buffer_out.writeFloat(this.w);
	}
}

export class Colour3f {
	r: number;
	g: number;
	b: number;

	constructor(x_, y_, z_) {
		this.r = x_;
		this.g = y_;
		this.b = z_;
	}

	writeToStream(buffer_out: BufferOut) {
		buffer_out.writeFloat(this.r);
		buffer_out.writeFloat(this.g);
		buffer_out.writeFloat(this.b);
	}
}

export class Vec3d {
	x: number;
	y: number;
	z: number;

	constructor(x_, y_, z_) {
		this.x = x_;
		this.y = y_;
		this.z = z_;
	}

	writeToStream(buffer_out: BufferOut) {
		buffer_out.writeDouble(this.x);
		buffer_out.writeDouble(this.y);
		buffer_out.writeDouble(this.z);
	}
}

export function readVec2dFromStream(buffer_in: BufferIn) {
	let x = readDouble(buffer_in);
	let y = readDouble(buffer_in);
	return new Vec2d(x, y);
}

export function readVec3fFromStream(buffer_in: BufferIn) {
	let x = readFloat(buffer_in);
	let y = readFloat(buffer_in);
	let z = readFloat(buffer_in);
	return new Vec3f(x, y, z);
}

export function readVec3dFromStream(buffer_in: BufferIn) {
	let x = readDouble(buffer_in);
	let y = readDouble(buffer_in);
	let z = readDouble(buffer_in);
	return new Vec3d(x, y, z);
}

export function readColour3fFromStream(buffer_in: BufferIn) {
	let x = readFloat(buffer_in);
	let y = readFloat(buffer_in);
	let z = readFloat(buffer_in);
	return new Colour3f(x, y, z);
}

export function readMatrix2fFromStream(buffer_in: BufferIn) {
	let x = readFloat(buffer_in);
	let y = readFloat(buffer_in);
	let z = readFloat(buffer_in);
	let w = readFloat(buffer_in);
	return new Matrix2f(x, y, z, w);
}


export function readParcelIDFromStream(buffer_in: BufferIn) {
	return readUInt32(buffer_in);
}

export function readUserIDFromStream(buffer_in: BufferIn) {
	return readUInt32(buffer_in);
}

const TIMESTAMP_SERIALISATION_VERSION = 1;

export function readTimeStampFromStream(buffer_in: BufferIn): bigint {
	let version = readUInt32(buffer_in);
	if (version != TIMESTAMP_SERIALISATION_VERSION)
		throw "Unhandled version " + version.toString() + ", expected " + TIMESTAMP_SERIALISATION_VERSION.toString() + ".";

	return readUInt64(buffer_in);
}

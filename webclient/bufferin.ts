/*=====================================================================
bufferin.ts
-----------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/


import { fromUTF8Array } from './utils.js'


export class BufferIn {
	
	array_buffer : ArrayBuffer;
	data_view : DataView;
	read_index : number;
	
	constructor(array_buffer_ : ArrayBuffer) {
		this.array_buffer = array_buffer_;
		this.data_view = new DataView(array_buffer_);
		this.read_index = 0;
	}

	getReadIndex(): number {
		return this.read_index;
	}

	setReadIndex(index: number) {
		this.read_index = index;
	}

	length(): number {
		return this.array_buffer.byteLength;
	}

	endOfStream(): boolean {
		return this.read_index >= this.array_buffer.byteLength;
	}

	readData(len): ArrayBuffer {
		var res = this.array_buffer.slice(this.read_index, this.read_index + len);
		this.read_index += len;
		return res;
	}

	readInt32(): number {
		var x = this.data_view.getInt32(/*byte offset=*/this.read_index, /*little endian=*/true);
		this.read_index += 4;
		return x;
	}

	readUInt32(): number {
		var x = this.data_view.getUint32(/*byte offset=*/this.read_index, /*little endian=*/true);
		this.read_index += 4;
		return x;
	}
	
	// See https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/DataView
	// Typescript compiler complains about getBigUint64() 
	//getUint64(dataview, byteOffset, littleEndian) {
	//	// split 64-bit number into two 32-bit (4-byte) parts
	//	const left =  dataview.getUint32(byteOffset, littleEndian);
	//	const right = dataview.getUint32(byteOffset+4, littleEndian);
	//
	//	// combine the two 32-bit values
	//	const combined = littleEndian? left + 2**32*right : 2**32*left + right;
	//
	//	//if (!Number.isSafeInteger(combined))
	//	//console.warn(combined, 'exceeds MAX_SAFE_INTEGER. Precision may be lost');
	//
	//	return combined;
	//}

	readUInt64(): bigint {
		var x = this.data_view.getBigUint64(/*byte offset=*/this.read_index, /*little endian=*/true);
		//var x = this.getUint64(this.data_view, /*byte offset=*/this.read_index, /*little endian=*/true)
		this.read_index += 8;
		return x;
	}

	readFloat(): number {
		var x = this.data_view.getFloat32(/*byte offset=*/this.read_index, /*little endian=*/true);
		this.read_index += 4;
		return x;
	}

	readDouble(): number {
		var x = this.data_view.getFloat64(/*byte offset=*/this.read_index, /*little endian=*/true);
		this.read_index += 8;
		return x;
	}

	readStringLengthFirst(): string {
		let len = this.readUInt32(); // Read length in bytes

		let utf8_array = new Int8Array(this.array_buffer, /*byteoffset=*/this.read_index, /*length=*/len);

		this.read_index += len;

		return fromUTF8Array(utf8_array);
	}
}




export function readInt32(buffer_in: BufferIn) {
	let x = buffer_in.data_view.getInt32(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
	buffer_in.read_index += 4;
	return x;
}

export function readUInt32(buffer_in: BufferIn) {
	let x = buffer_in.data_view.getUint32(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
	buffer_in.read_index += 4;
	return x;
}

export function readUInt64(buffer_in: BufferIn): bigint {
	let x = buffer_in.data_view.getBigUint64(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
	buffer_in.read_index += 8;
	return x;
}

export function readFloat(buffer_in: BufferIn) {
	let x = buffer_in.data_view.getFloat32(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
	buffer_in.read_index += 4;
	return x;
}

export function readDouble(buffer_in: BufferIn) {
	let x = buffer_in.data_view.getFloat64(/*byte offset=*/buffer_in.read_index, /*little endian=*/true);
	buffer_in.read_index += 8;
	return x;
}


export function readStringFromStream(buffer_in: BufferIn) {
	let len = readUInt32(buffer_in); // Read length in bytes

	let utf8_array = new Int8Array(buffer_in.array_buffer, /*byteoffset=*/buffer_in.read_index, /*length=*/len);

	buffer_in.read_index += len;

	return fromUTF8Array(utf8_array);
}

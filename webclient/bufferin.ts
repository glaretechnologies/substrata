/*=====================================================================
bufferin.ts
-----------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

// from https://gist.github.com/joni/3760795
function fromUTF8Array(data: Int8Array): string { // array of bytes
	var str = '',
		i;

	for (i = 0; i < data.length; i++) {
		var value = data[i];

		if (value < 0x80) {
			str += String.fromCharCode(value);
		} else if (value > 0xBF && value < 0xE0) {
			str += String.fromCharCode((value & 0x1F) << 6 | data[i + 1] & 0x3F);
			i += 1;
		} else if (value > 0xDF && value < 0xF0) {
			str += String.fromCharCode((value & 0x0F) << 12 | (data[i + 1] & 0x3F) << 6 | data[i + 2] & 0x3F);
			i += 2;
		} else {
			// surrogate pair
			var charCode = ((value & 0x07) << 18 | (data[i + 1] & 0x3F) << 12 | (data[i + 2] & 0x3F) << 6 | data[i + 3] & 0x3F) - 0x010000;

			str += String.fromCharCode(charCode >> 10 | 0xD800, charCode & 0x03FF | 0xDC00);
			i += 3;
		}
	}

	return str;
}


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

/*=====================================================================
bufferin.js
-----------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/

// from https://gist.github.com/joni/3760795
function fromUTF8Array(data) { // array of bytes
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
	constructor(array_buffer_) {
		this.array_buffer = array_buffer_;
		this.data_view = new DataView(array_buffer_);
		this.read_index = 0;
	}

	getReadIndex() {
		return this.read_index;
	}

	setReadIndex(index) {
		this.read_index = index;
	}

	length() {
		return this.array_buffer.length;
	}

	endOfStream() {
		return this.read_index >= this.array_buffer.byteLength;
	}

	readData(len) {
		var res = this.array_buffer.slice(this.read_index, this.read_index + len);
		this.read_index += len;
		return res;
	}

	readInt32() {
		var x = this.data_view.getInt32(/*byte offset=*/this.read_index, /*little endian=*/true);
		this.read_index += 4;
		return x;
	}

	readUInt32() {
		var x = this.data_view.getUint32(/*byte offset=*/this.read_index, /*little endian=*/true);
		this.read_index += 4;
		return x;
	}

	readUInt64() {
		var x = this.data_view.getBigUint64(/*byte offset=*/this.read_index, /*little endian=*/true);
		this.read_index += 8;
		return x;
	}

	readFloat() {
		var x = this.data_view.getFloat32(/*byte offset=*/this.read_index, /*little endian=*/true);
		this.read_index += 4;
		return x;
	}

	readDouble() {
		var x = this.data_view.getFloat64(/*byte offset=*/this.read_index, /*little endian=*/true);
		this.read_index += 8;
		return x;
	}

	readStringLengthFirst() {
		let len = this.readUInt32(); // Read length in bytes

		let utf8_array = new Int8Array(this.array_buffer, /*byteoffset=*/this.read_index, /*length=*/len);

		this.read_index += len;

		return fromUTF8Array(utf8_array);
	}
}

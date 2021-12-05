/*=====================================================================
bufferin.js
-----------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/


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
}

/*=====================================================================
bufferout.js
------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/


import { toUTF8Array } from './utils.js'


export class BufferOut {

    data: ArrayBuffer;
    data_view: DataView;
    size: number;

    constructor() {
        this.data = new ArrayBuffer(/*length=*/256);
        this.data_view = new DataView(this.data);
        this.size = 0;
    }

    checkForResize(newsize: number) {
        if (newsize > this.data.byteLength) {
            //console.log("BufferOut: resizing data to size " + this.data.byteLength * 2 + " B")
            // Resize data
            let olddata = this.data;
            let newdata = new ArrayBuffer(/*length=*/this.data.byteLength * 2); // alloc new array

            this.data = newdata;
            this.data_view = new DataView(this.data);

            // copy old data to new data
            let old_data_view = new DataView(olddata);

            for (let i = 0; i < olddata.byteLength; ++i)
                this.data_view.setUint8(i, old_data_view.getInt8(i));
        }
    }

    writeInt32(x: number) {
        this.checkForResize(/*newsize=*/this.size + 4);

        this.data_view.setInt32(/*byte offset=*/this.size, x, /*little endian=*/true);
        this.size += 4;
    }

    writeUInt32(x: number) {
        this.checkForResize(/*newsize=*/this.size + 4);

        this.data_view.setUint32(/*byte offset=*/this.size, x, /*little endian=*/true);
        this.size += 4;
    }

    writeUInt64(x: bigint) {
        this.checkForResize(/*newsize=*/this.size + 8);

        this.data_view.setBigUint64(/*byte offset=*/this.size, BigInt(x), /*little endian=*/true);
        this.size += 8;
    }

    writeFloat(x: number) {
        this.checkForResize(/*newsize=*/this.size + 4);

        this.data_view.setFloat32(/*byte offset=*/this.size, x, /*little endian=*/true);
        this.size += 4;
    }

    writeDouble(x: number) {
        this.checkForResize(/*newsize=*/this.size + 8);

        this.data_view.setFloat64(/*byte offset=*/this.size, x, /*little endian=*/true);
        this.size += 8;
    }

    writeStringLengthFirst(str: string) {
        let utf8_array = toUTF8Array(str)

        this.writeUInt32(utf8_array.length);

        let write_i = this.size;
        this.checkForResize(/*newsize=*/this.size + utf8_array.length);

        for (let i = 0; i < utf8_array.length; ++i)
            this.data_view.setUint8(write_i + i, utf8_array[i]);

        this.size += utf8_array.length;
    }


    updateMessageLengthField() {
        this.data_view.setUint32(/*byte offset=*/4, /*value=*/this.size, /*little endian=*/true);
    }

    writeToWebSocket(web_socket: WebSocket) {
        // console.log("writeToWebSocket(): this.size:" + this.size)

        let trimmed = this.data.slice(0, this.size);

        web_socket.send(trimmed);
    }

    writeBufferOut(buffer: BufferOut) {
        let write_i = this.size;
        this.checkForResize(/*newsize=*/this.size + buffer.size);

        for (let i = 0; i < buffer.size; ++i)
            this.data_view.setUint8(write_i + i, buffer.data_view.getUint8(i)); // NOTE: can no doubt be optimised to avoid doing this byte by byte

        this.size += buffer.size;
    }
}

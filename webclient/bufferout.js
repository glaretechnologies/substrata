/*=====================================================================
bufferout.js
------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/


// from https://gist.github.com/joni/3760795
function toUTF8Array(str) {
    var utf8 = [];
    for (var i = 0; i < str.length; i++) {
        var charcode = str.charCodeAt(i);
        if (charcode < 0x80) utf8.push(charcode);
        else if (charcode < 0x800) {
            utf8.push(0xc0 | (charcode >> 6),
                0x80 | (charcode & 0x3f));
        }
        else if (charcode < 0xd800 || charcode >= 0xe000) {
            utf8.push(0xe0 | (charcode >> 12),
                0x80 | ((charcode >> 6) & 0x3f),
                0x80 | (charcode & 0x3f));
        }
        // surrogate pair
        else {
            i++;
            // UTF-16 encodes 0x10000-0x10FFFF by
            // subtracting 0x10000 and splitting the
            // 20 bits of 0x0-0xFFFFF into two halves
            charcode = 0x10000 + (((charcode & 0x3ff) << 10)
                | (str.charCodeAt(i) & 0x3ff))
            utf8.push(0xf0 | (charcode >> 18),
                0x80 | ((charcode >> 12) & 0x3f),
                0x80 | ((charcode >> 6) & 0x3f),
                0x80 | (charcode & 0x3f));
        }
    }
    return utf8;
}


export class BufferOut {

    constructor() {
        this.data = new ArrayBuffer(/*length=*/256);
        this.data_view = new DataView(this.data);
        this.size = 0;
    }

    checkForResize(newsize) {
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

    writeInt32(x) {
        this.checkForResize(/*newsize=*/this.size + 4);

        this.data_view.setInt32(/*byte offset=*/this.size, x, /*little endian=*/true);
        this.size += 4;
    }

    writeUInt32(x) {
        this.checkForResize(/*newsize=*/this.size + 4);

        this.data_view.setUint32(/*byte offset=*/this.size, x, /*little endian=*/true);
        this.size += 4;
    }

    writeUInt64(x) {
        this.checkForResize(/*newsize=*/this.size + 8);

        this.data_view.setBigUint64(/*byte offset=*/this.size, BigInt(x), /*little endian=*/true);
        this.size += 8;
    }

    writeFloat(x) {
        this.checkForResize(/*newsize=*/this.size + 4);

        this.data_view.setFloat32(/*byte offset=*/this.size, x, /*little endian=*/true);
        this.size += 4;
    }

    writeDouble(x) {
        this.checkForResize(/*newsize=*/this.size + 8);

        this.data_view.setFloat64(/*byte offset=*/this.size, x, /*little endian=*/true);
        this.size += 8;
    }

    writeStringLengthFirst(str) {
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

    writeToWebSocket(web_socket) {
        // console.log("writeToWebSocket(): this.size:" + this.size)

        let trimmed = this.data.slice(0, this.size);

        web_socket.send(trimmed);
    }
}

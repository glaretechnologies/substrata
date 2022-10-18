/*=====================================================================
generators.ts
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/

// Generate a tessellated patch in [-1, -1] x [1, 1] of cols x rows quads
export function generatePatch (cols: number, rows: number): [Float32Array, Uint32Array | Uint16Array] {
	const W = (cols + 1), H = (rows + 1);
	const vcount = W * H, icount = cols * rows * 2;
	const vbuf = new Float32Array(vcount * 3);
	const ibuf = vcount > 65535 ? new Uint32Array(icount * 3) : new Uint16Array(icount * 3);
	const dw = 2./cols, dh = 2./rows;

	for(let r = 0; r !== H; ++r) {
		const off = 3 * r * W;
		for(let c = 0; c !== W; ++c) {
			let ii = 3 * c + off;
			vbuf[ii++] = -1. + c * dw;
			vbuf[ii++] = -1. + r * dh;
			vbuf[ii] = 0;
		}
	}

	for(let r = 0; r !== H; ++r) {
		const off = 6 * r * cols;
		for(let c = 0; c !== W; ++c) {
			const ii = 6 * c + off;
			ibuf[ii] = (r + 1) * W + c;
			ibuf[ii+1] = r * W + c;
			ibuf[ii+2] = ibuf[ii+1] + 1;
			ibuf[ii+3] = ibuf[ii];
			ibuf[ii+4] = ibuf[ii+2];
			ibuf[ii+5] = ibuf[ii] + 1;
		}
	}
	return [vbuf, ibuf];
}

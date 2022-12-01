/*=====================================================================
shaderMaterial.ts
----------------
Copyright Glare Technologies Limited 2022 -

This shader adds some customisation to the MeshStandardMaterial of
three.js.  Allows specification of geometric normal calculation or
normal attributes.

Add support for outline rendering?

Taking the approach recommended here:

https://discourse.threejs.org/t/modifying-standardmaterial-or-custom-shadermaterial/37692
=====================================================================*/

import * as THREE from './build/three.module.js';

// language=glsl
function genNormalString (geoNormals: boolean): string {
	return `
	float faceDirection = gl_FrontFacing ? 1.0 : - 1.0;

	vec3 normal = ${geoNormals 
		? 'normalize(cross(dFdx(vViewPosition), dFdy(vViewPosition)));'
		: 'normalize(vNormal);'}

	#ifdef DOUBLE_SIDED

	normal = normal * faceDirection;

	#endif

	#ifdef USE_TANGENT

	vec3 tangent = normalize( vTangent );
	vec3 bitangent = normalize( vBitangent );

	#ifdef DOUBLE_SIDED
	
		tangent = tangent * faceDirection;
		bitangent = bitangent * faceDirection;
	
	#endif

	#if defined( TANGENTSPACE_NORMALMAP ) || defined( USE_CLEARCOAT_NORMALMAP )
	
		mat3 vTBN = mat3( tangent, bitangent, normal );
	
	#endif

	#endif

	// Required by three for clearCoat

	vec3 geometryNormal = normal;
`;
}

// Returns the source string if not found
function insertAfter (source: string, search: string, seq: string): string {
	const start = source.indexOf(search);
	return start !== -1
		? source.slice(0, start + search.length) + seq + source.slice(start + search.length + 1)
		: source;
}

// Returns source string if search not found
function replace (source: string, search: string, seq: string): string {
	const start = source.indexOf(search);
	return start !== -1
		? source.slice(0, start) + seq + source.slice(start + search.length + 1)
		: source;
}

// Accepted Settings
export interface CustomShaderConf {
	geoNormals?: boolean // Calculate geometric normals based on view position derivatives
	generateUVs?: boolean // Generate repeating uv coordinates based on object space
}

export class CustomStandardMaterial extends THREE.MeshStandardMaterial {
	private readonly conf: CustomShaderConf;

	public constructor (parameters?: Record<string, any>, conf?: CustomShaderConf) {
		super(parameters);
		this.conf = {
			geoNormals: conf?.geoNormals ?? false,
			generateUVs: conf?.generateUVs ?? false
		};
	}

	// This function should generate a key based on the configuration of the shader allowing three to cache the
	// shader definition internally.
	public customProgramCacheKey (): string {
		let progKey = 'cust-';

		progKey += (this.conf.geoNormals ? 'geo-' : '');
		progKey += (this.conf.generateUVs ? 'uv-' : '');
		progKey += 'shdr';

		return progKey;
	}

	private genVertexUV (vShdr: string): string {
		if(!this.conf.generateUVs) return vShdr;

		const uvDefs = '#include <uv_pars_vertex>';
		const uvCode = '#include <uv_vertex>';

		const defStr = 'varying vec3 modelP;\n';
		const codeStr = 'modelP = position;\n';

		const a = replace(vShdr, uvDefs, defStr);
		return replace(a, uvCode, codeStr);
	}

	private genFragmentUV (fShdr: string): string {
		if(!this.conf.generateUVs) return fShdr;

		const uvDefs = '#include <uv_pars_fragment>'; 			   // Note: fragment has no uv insertion point defined,
		const uvCode = '#include <clipping_planes_fragment>';  // so be put it at the end of the first block...

		const defStr = `
			varying vec3 modelP;
			uniform mat3 uvTransform;
		`;

		const codeStr = `
			vec2 vUv = vec2(0.);
			vec3 dPdx = dFdx(modelP);
			vec3 dPdy = dFdy(modelP);
			vec3 modelN = cross(dPdx, dPdy);
			vec3 sgn = sign(modelN);
			modelN = abs(modelN);
			
			if(modelN.x > modelN.y && modelN.x > modelN.z) {
			  vUv.x = modelP.y * sgn.x;
			  vUv.y = modelP.z;
			} else if(modelN.y > modelN.x && modelN.y > modelN.z) {
				vUv.x = modelP.x * -sgn.y;
				vUv.y = modelP.z;
			} else {
				vUv.x = modelP.x * sgn.z;
				vUv.y = modelP.y;
			}
			
			vUv = (uvTransform * vec3(vUv, 1.)).xy;
		`;

		const a = replace(fShdr, uvDefs, defStr);
		return insertAfter(a, uvCode, codeStr);
	}

	private genFragmentNormal (fShdr: string): string {
		const normalInsertion = '#include <normal_fragment_begin>';
		return replace(fShdr, normalInsertion, genNormalString(this.conf.geoNormals));
	}

	// This overridden function is called by three before compiling the shader.
	public onBeforeCompile (shader: THREE.Shader, rndr: THREE.WebGL2Renderer) {
		// Process vertex shader...
		const vertShader = this.genVertexUV(shader.vertexShader);

		// Process fragment shader...
		let	fragShader = this.genFragmentNormal(shader.fragmentShader);
		fragShader = this.genFragmentUV(fragShader);

		shader.vertexShader = vertShader;
		shader.fragmentShader = fragShader;
	}
}

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

export class CustomStandardMaterial extends THREE.MeshStandardMaterial {
	private readonly geometricNormals: boolean;

	public constructor (parameters?: any, geometricNormals?: boolean) { // Three hates TS
		super(parameters);
		this.geometricNormals = geometricNormals ?? false;
	}

	public customProgramCacheKey (): string {
		return this.geometricNormals ? '1' : '0';
	}

	public onBeforeCompile (shader: THREE.Shader, rndr: THREE.WebGL2Renderer) {
		const insertionPoint = '#include <normal_fragment_begin>';

		const shaderDef: string = shader.fragmentShader;
		const start = shaderDef.indexOf(insertionPoint);
		if(start !== -1) {
			const end = start + insertionPoint.length + 1;
			shader.fragmentShader = shaderDef.slice(0, start)
				+ genNormalString(this.geometricNormals)
				+	shaderDef.slice(end);
		} else {
			console.warn('Cannot find insertion point for shader customisation');
		}
	}
}


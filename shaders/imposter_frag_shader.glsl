#version 330 core

in vec3 normal_cs;
in vec3 normal_ws;
in vec3 pos_cs;
in vec3 pos_ws;
in vec2 texture_coords;
#if NUM_DEPTH_TEXTURES > 0
in vec3 shadow_tex_coords[NUM_DEPTH_TEXTURES];
#endif
in vec3 cam_to_pos_ws;
#if USE_LOGARITHMIC_DEPTH_BUFFER
in float flogz;
#endif

uniform sampler2D diffuse_tex;
uniform sampler2DShadow dynamic_depth_tex;
uniform sampler2DShadow static_depth_tex;
uniform samplerCube cosine_env_tex;
uniform sampler2D blue_noise_tex;
uniform sampler2D fbm_tex;

layout (std140) uniform PhongUniforms
{
	vec4 sundir_cs;
	vec4 diffuse_colour;
	mat3 texture_matrix;
	int have_shading_normals;
	int have_texture;
	float roughness;
	float fresnel_scale;
	float metallic_frac;
	float time;
	float begin_fade_out_distance;
	float end_fade_out_distance;
};


out vec4 colour_out;


vec2 samples[16] = vec2[](
	vec2(-0.00033789, -0.00072656),
	vec2(0.00056445, -0.00064844),
	vec2(0.000013672, -0.00054883),
	vec2(-0.00058594, -0.00011914),
	vec2(-0.00017773, -0.00021094),
	vec2(0.00019336, -0.00019336),
	vec2(-0.000019531, -0.000058594),
	vec2(-0.00022070, 0.00014453),
	vec2(0.000089844, 0.000068359),
	vec2(0.00036328, 0.000058594),
	vec2(0.00061328, 0.000087891),
	vec2(-0.0000078125, 0.00028906),
	vec2(-0.00089453, 0.00043750),
	vec2(-0.00022852, 0.00058984),
	vec2(0.00019336, 0.00042188),
	vec2(0.00071289, -0.00025977)
	);

vec3 toNonLinear(vec3 x)
{
	// Approximation to pow(x, 0.4545).  Max error of ~0.004 over [0, 1].
	return 0.124445006f*x*x + -0.35056138f*x + 1.2311935*sqrt(x);
}


float fbm(vec2 p)
{
	return (texture(fbm_tex, p).x - 0.5) * 2.f;
}

vec2 rot(vec2 p)
{
	float theta = 1.618034 * 3.141592653589 * 2;
	return vec2(cos(theta) * p.x - sin(theta) * p.y, sin(theta) * p.x + cos(theta) * p.y);
}

float fbmMix(vec2 p)
{
	return 
		fbm(p) +
		fbm(rot(p * 2)) * 0.5 +
		0;
}

float sampleDynamicDepthMap(mat2 R, vec3 shadow_coords)
{
	float sum = 0;
	for(int i = 0; i < 16; ++i)
	{
		vec2 st = shadow_coords.xy + R * samples[i];
		sum += texture(dynamic_depth_tex, vec3(st.x, st.y, shadow_coords.z));
	}
	return sum * (1.f / 16);
}


float sampleStaticDepthMap(mat2 R, vec3 shadow_coords)
{
	float sum = 0;
	for(int i = 0; i < 16; ++i)
	{
		vec2 st = shadow_coords.xy + R * samples[i];
		sum += texture(static_depth_tex, vec3(st.x, st.y, shadow_coords.z));
	}
	return sum * (1.f / 16);
}


void main()
{
	vec3 use_normal_cs;
	vec3 use_normal_ws;
	vec2 use_texture_coords = texture_coords;
	if(have_shading_normals != 0)
	{
		use_normal_cs = normal_cs;
		use_normal_ws = normal_ws;
	}
	else
	{
		// Compute camera-space geometric normal.
		vec3 dp_dx = dFdx(pos_cs);
		vec3 dp_dy = dFdy(pos_cs);
		vec3 N_g = cross(dp_dx, dp_dy);
		use_normal_cs = N_g;

		// Compute world-space geometric normal.
		dp_dx = dFdx(pos_ws);
		dp_dy = dFdy(pos_ws);
		N_g = cross(dp_dx, dp_dy);
		use_normal_ws = N_g;
	}
	vec3 unit_normal_cs = normalize(use_normal_cs);

	float light_cos_theta = max(dot(unit_normal_cs, sundir_cs.xyz), 0.0);

	vec3 frag_to_cam = normalize(pos_cs * -1.0);

	vec3 h = normalize(frag_to_cam + sundir_cs.xyz);

	// Work out which imposter sprite to use
	vec3 to_frag_proj = normalize(vec3(pos_cs.x, 0.f, pos_cs.z)); // Projected onto ground plane
	vec3 to_sun_proj = normalize(vec3(sundir_cs.x, 0.f, sundir_cs.z)); // Projected onto ground plane
	vec3 right_cs = cross(to_frag_proj, vec3(0,1,0)); // to fragment, projeted onto groundf plane

	float r = dot(right_cs, to_sun_proj);

	int sprite_a, sprite_b;
	float sprite_blend_frac;
	if(dot(pos_cs, sundir_cs.xyz) > 0) // Backlighting
	{
		if(r > 0)
		{
			sprite_a = 3; // rightlit
			sprite_b = 0; // backlit

			sprite_blend_frac = 1 - r;
		}
		else
		{
			sprite_a = 2; // leftlit
			sprite_b = 0; // backlit

			sprite_blend_frac = 1 + r; // 1 - -r
		}
	}
	else // Else frontlighting
	{
		if(r > 0)
		{
			sprite_a = 3; // rightlit
			sprite_b = 1; // frontlit
			
			sprite_blend_frac = 1 - r;
		}
		else
		{
			sprite_a = 2; // leftlit
			sprite_b = 1; // frontlit

			sprite_blend_frac = 1 + r; // 1 - -r
		}
	}

	float sprite_a_x = 0.25f * sprite_a + use_texture_coords.x * 0.25f;
	float sprite_b_x = 0.25f * sprite_b + use_texture_coords.x * 0.25f;

	vec4 texture_diffuse_col;
	if(have_texture != 0)
	{
		vec4 col_a = texture(diffuse_tex, (texture_matrix * vec3(sprite_a_x, use_texture_coords.y, 1.0)).xy);
		vec4 col_b = texture(diffuse_tex, (texture_matrix * vec3(sprite_b_x, use_texture_coords.y, 1.0)).xy);

		texture_diffuse_col = mix(col_a, col_b, sprite_blend_frac);

#if CONVERT_ALBEDO_FROM_SRGB
		// Texture value is in non-linear sRGB, convert to linear sRGB.
		// See http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html, expression for C_lin_3.
		vec4 c = texture_diffuse_col;
		vec4 c2 = c * c;
		texture_diffuse_col = c * c2 * 0.305306011f + c2 * 0.682171111f + c * 0.012522878f;
#endif
	}
	else
		texture_diffuse_col = vec4(1.f);

	vec4 diffuse_col = texture_diffuse_col * diffuse_colour; // diffuse_colour is linear sRGB already.
	diffuse_col.xyz *= 0.8f; // Just a hack scale to make the brightnesses look similar

	float pixel_hash = texture(blue_noise_tex, gl_FragCoord.xy * (1 / 128.f)).x;
	float dist_alpha_factor = smoothstep(100.f, 120.f,  /*dist=*/-pos_cs.z);
	if(dist_alpha_factor < pixel_hash) // Draw imposter only when dist_alpha_factor >= pixel_hash, draw real object when dist_alpha_factor <= pixel_hash
		discard;

#if ALPHA_TEST
	if(diffuse_col.a < 0.5f)
		discard;
#endif

	// Shadow mapping
	float sun_vis_factor;
#if SHADOW_MAPPING

#define VISUALISE_CASCADES 0

	float samples_scale = 1.f;

	float pattern_theta = pixel_hash * 6.283185307179586;
	mat2 R = mat2(cos(pattern_theta), sin(pattern_theta), -sin(pattern_theta), cos(pattern_theta));

	sun_vis_factor = 0.0;

	float dist = -pos_cs.z;
	if(dist < DEPTH_TEXTURE_SCALE_MULT*DEPTH_TEXTURE_SCALE_MULT)
	{
		if(dist < DEPTH_TEXTURE_SCALE_MULT) // if dynamic_depth_tex_index == 0:
		{
			float tex_0_vis = sampleDynamicDepthMap(R, shadow_tex_coords[0]);

			float edge_dist = 0.8f * DEPTH_TEXTURE_SCALE_MULT;
			if(dist > edge_dist)
			{
				float tex_1_vis = sampleDynamicDepthMap(R, shadow_tex_coords[1]);

				float blend_factor = smoothstep(edge_dist, DEPTH_TEXTURE_SCALE_MULT, dist);
				sun_vis_factor = mix(tex_0_vis, tex_1_vis, blend_factor);
			}
			else
				sun_vis_factor = tex_0_vis;

#if VISUALISE_CASCADES
			diffuse_col.yz *= 0.5f;
#endif
		}
		else
		{
			sun_vis_factor = sampleDynamicDepthMap(R, shadow_tex_coords[1]);

			float edge_dist = 0.6f * (DEPTH_TEXTURE_SCALE_MULT * DEPTH_TEXTURE_SCALE_MULT);

			// Blending with static shadow map 0
			if(dist > edge_dist)
			{
				vec3 static_shadow_cds = shadow_tex_coords[NUM_DYNAMIC_DEPTH_TEXTURES];

				float static_sun_vis_factor = sampleStaticDepthMap(R, static_shadow_cds); // NOTE: had 0.999f cap and bias of 0.0005: min(static_shadow_cds.z, 0.999f) - bias

				float blend_factor = smoothstep(edge_dist, DEPTH_TEXTURE_SCALE_MULT * DEPTH_TEXTURE_SCALE_MULT, dist);
				sun_vis_factor = mix(sun_vis_factor, static_sun_vis_factor, blend_factor);
			}

#if VISUALISE_CASCADES
			diffuse_col.yz *= 0.75f;
#endif
		}
	}
	else
	{
		float l1dist = dist;

		if(l1dist < 1024)
		{
			int static_depth_tex_index;
			float cascade_end_dist;
			if(l1dist < 64)
			{
				static_depth_tex_index = 0;
				cascade_end_dist = 64;
			}
			else if(l1dist < 256)
			{
				static_depth_tex_index = 1;
				cascade_end_dist = 256;
			}
			else
			{
				static_depth_tex_index = 2;
				cascade_end_dist = 1024;
			}

			vec3 shadow_cds = shadow_tex_coords[static_depth_tex_index + NUM_DYNAMIC_DEPTH_TEXTURES];
			sun_vis_factor = sampleStaticDepthMap(R, shadow_cds); // NOTE: had cap and bias

#if DO_STATIC_SHADOW_MAP_CASCADE_BLENDING
			if(static_depth_tex_index < NUM_STATIC_DEPTH_TEXTURES - 1)
			{
				float edge_dist = 0.7f * cascade_end_dist;

				// Blending with static shadow map static_depth_tex_index + 1
				if(l1dist > edge_dist)
				{
					int next_tex_index = static_depth_tex_index + 1;
					vec3 next_shadow_cds = shadow_tex_coords[next_tex_index + NUM_DYNAMIC_DEPTH_TEXTURES];

					float next_sun_vis_factor = sampleStaticDepthMap(R, next_shadow_cds); // NOTE: had cap and bias

					float blend_factor = smoothstep(edge_dist, cascade_end_dist, l1dist);
					sun_vis_factor = mix(sun_vis_factor, next_sun_vis_factor, blend_factor);
				}
			}
#endif

#if VISUALISE_CASCADES
			diffuse_col.xz *= float(static_depth_tex_index) / NUM_STATIC_DEPTH_TEXTURES;
#endif
		}
		else
			sun_vis_factor = 1.0;
	}

#else
	sun_vis_factor = 1.0;
#endif

	// Apply cloud shadows
	// Compute position on cumulus cloud layer
	if(pos_ws.z < 1000.f)
	{
		vec3 sundir_ws = vec3(3.6716393E-01, 6.3513672E-01, 6.7955279E-01); // TEMP HACK
		vec3 cum_layer_pos = pos_ws + sundir_ws * (1000.f - pos_ws.z) / sundir_ws.z;

		vec2 cum_tex_coords = vec2(cum_layer_pos.x, cum_layer_pos.y) * 1.0e-4f;
		cum_tex_coords.x += time * 0.002;

		vec2 cumulus_coords = vec2(cum_tex_coords.x * 2 + 2.3453, cum_tex_coords.y * 2 + 1.4354);
		float cumulus_val = max(0.f, fbmMix(cumulus_coords) - 0.3f);

		float cumulus_trans = max(0.f, 1.f - cumulus_val * 1.4);
		sun_vis_factor *= cumulus_trans;
	}

	vec3 unit_normal_ws = normalize(use_normal_ws);
	if(dot(unit_normal_ws, cam_to_pos_ws) > 0)
		unit_normal_ws = -unit_normal_ws;

	vec4 sky_irradiance;
	sky_irradiance = texture(cosine_env_tex, unit_normal_ws.xyz) * 1.0e9f; // integral over hemisphere of cosine * incoming radiance from sky.


	vec4 sun_light = vec4(1662102582.6479533,1499657101.1924045,1314152016.0871031, 1) * sun_vis_factor; // Sun spectral radiance multiplied by solid angle, see SkyModel2Generator::makeSkyEnvMap().

	vec4 col =
		sky_irradiance * diffuse_col * (1.0 / 3.141592653589793) +  // Diffuse substrate part of BRDF * incoming radiance from sky
		sun_light * diffuse_col * (1.0 / 3.141592653589793); //  Diffuse substrate part of BRDF * sun light

#if DEPTH_FOG
	// Blend with background/fog colour
	float dist_ = -pos_cs.z;
	float fog_factor = 1.0f - exp(dist_ * -0.00015);
	vec4 sky_col = vec4(1.8, 4.7, 8.0, 1) * 2.0e7; // Bluish grey
	col = mix(col, sky_col, fog_factor);
#endif

	col *= 0.000000003; // tone-map

	colour_out = vec4(toNonLinear(col.xyz), 1);
	colour_out.w = diffuse_col.a;

#if USE_LOGARITHMIC_DEPTH_BUFFER
	float farplane = 10000.0;
	float Fcoef = 2.0 / log2(farplane + 1.0);
	float Fcoef_half = 0.5 * Fcoef;
	gl_FragDepth = log2(flogz) * Fcoef_half;
#endif
}


in vec3 pos_os;
in vec3 pos_cs;
in vec2 texture_coords;

uniform float time;
uniform vec3 colour;
uniform vec3 campos_os;

#if ORDER_INDEPENDENT_TRANSPARENCY
// Various outputs for order-independent transparency.
layout(location = 0) out vec4 transmittance_out;
layout(location = 1) out vec4 accum_out;
#else
layout(location = 0) out vec4 colour_out;
#endif

uniform sampler2D fbm_tex;


float rayPlaneIntersect(vec3 raystart, vec3 ray_unitdir, float plane_h)
{
	float start_to_plane_dist = raystart.x - plane_h;

	return start_to_plane_dist / -ray_unitdir.x;
}


// https://www.shadertoy.com/view/MdcfDj
// LICENSE: http://unlicense.org/
#define M1 1597334677U     //1719413*929
#define M2 3812015801U     //140473*2467*11
float hash( uvec2 q )
{
	q *= uvec2(M1, M2); 

	uint n = (q.x ^ q.y) * M1;

	return float(n) * (1.0/float(0xffffffffU));
}


#define HASH_VALUE_THRESHOLD 0.97

float innerEvalSlightlyJittered(vec2 p)
{
    ivec2 cell_coords = ivec2(floor(p));
    float h = hash(uvec2(ivec2(10000000) + cell_coords));
    
    float v = 0.0;
    if(h >= HASH_VALUE_THRESHOLD)
    {
         vec2 star_centre = vec2(cell_coords) + vec2(0.25) + 
             vec2(
                 hash(uvec2(cell_coords) + uvec2(0, 134324)),
                 hash(uvec2(cell_coords) + uvec2(73454, 234324))
             ) * 0.5;
         float d = length(p - star_centre);
         v = pow(max(0.0, 1.0 - d*2.0), 4.0);
    }
    return v;
}


float evalStarfield(vec2 p)
{   
    vec2 dcoords_dx = dFdx(p);
    vec2 dcoords_dy = dFdy(p);

    float rxf = clamp(length(dcoords_dx) * 3.0, 1.0, 8.0);
    float ryf = clamp(length(dcoords_dy) * 3.0, 1.0, 8.0);

    int rx = int(ceil(rxf));
    int ry = int(ceil(ryf));

    float v = 0.0;
    for(int x=0; x<rx; ++x)
    for(int y=0; y<ry; ++y)
    {
        vec2 coords = p +
            (dcoords_dx * float(x)) / float(rx) + 
            (dcoords_dy * float(y)) / float(ry);
            
        v += innerEvalSlightlyJittered(coords);
    }
    v /= float(rx*ry);

    return v;
}


void main()
{
	vec3 unit_cam_to_pos_os = normalize(pos_os - campos_os);

	vec3 accum = vec3(0.0);

	
	float time_offset = time * -sign(campos_os.y);

	for(int side=0; side<2; ++side)
	{
		float side_f = float(side);
		const float wall_d = 0.8;
		float ray_t = rayPlaneIntersect(pos_os, unit_cam_to_pos_os, -wall_d + side_f * (wall_d*2.0));
		if(ray_t > 0.0)
		{
			vec3 hitpos = pos_os + unit_cam_to_pos_os * ray_t;

			for(int i=0; i<5; ++i)
			{
				float q = float(i) * (1.0 / 40.0);
				vec3 col = (vec3(1.0) + cos(vec3(time_offset * 0.7 + q*3.0) + vec3(6.0, 1.0, 2.0))) * vec3(0.1, 0.6, 1.0);
				accum += max(0.0, texture(fbm_tex, 
					vec2(q +/*q +*/ (hitpos.y /*+ q*5*/) * (0.2 /*+ q*/) * 0.01, hitpos.z + side_f * 3.0 + q*3.0) * 0.5 + vec2(-time_offset * (0.2 + q*0.3), 0.3)).x - 0.8) 
					* col * 1.0;
			}

			float star_env = 1.0 - smoothstep(10.0, 40.0, abs(hitpos.y));
			for(int i=0; i<6; ++i)
			{
				float q = float(i) * (1.0 / 40.0);
				vec3 col = (vec3(1.0) + cos(vec3(time_offset * 0.3 + q*3.0) + vec3(6.0, 1.0, 2.0))) * vec3(0.1, 0.6, 1.0);
                float scale = 10.0;
				float h = evalStarfield(vec2(hitpos.y*0.1*scale + side_f*3.5 - time_offset*1.0*(5.0 /*+ q*4.0*/), hitpos.z*scale /*+ q *//*+ side_f*5.5*/));
				accum += vec3(max(0.0, h)) * col * star_env * 4.0;
			}
		}
	}
	

	float T = 0.2;
#if ORDER_INDEPENDENT_TRANSPARENCY
	accum_out = vec4(accum * 10.0, 0.0);
	transmittance_out = vec4(T, T, T, T);
#else
	colour_out = vec4(accum * 10.0, 1.0 - T);
#endif
}

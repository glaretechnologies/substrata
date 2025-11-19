
in vec3 pos_os;
in vec2 texture_coords;

uniform float time;
uniform vec3 colour;

// Various outputs for order-independent transparency.
layout(location = 0) out vec4 transmittance_out;
layout(location = 1) out vec4 accum_out;


uniform sampler2D fbm_tex;


void main()
{
	float turbluence_speed = 0.4;
	vec2 offset = vec2(
		texture(fbm_tex, pos_os.xz * 3.0 + vec2(0.f, -time * turbluence_speed)).x - 0.5,
		texture(fbm_tex, pos_os.xz * 3.0 + vec2(-time * turbluence_speed, 0.0f)).x - 0.5
	) * 0.1;

	vec2 p = (pos_os.xz - vec2(0.07, 1.7));
	p += offset;

	float dist_to_ring = abs(length(p) - 0.5);
	float d = 0.1 / (dist_to_ring + 0.03);

	float a = atan(p.y, p.x);
	
	accum_out += (vec4(1.0) + cos(vec4(d - a*1.0 + time * 1.0) + vec4(6.0, 1.0, 2.0, 0.0))) * 0.3 * d;

	float T = min(1.0, dist_to_ring * 0.4);
	transmittance_out = vec4(T, T, T, T);
}

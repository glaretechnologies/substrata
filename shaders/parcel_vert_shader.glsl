#version 150

in vec3 position_in;
in vec3 normal_in;
in vec2 texture_coords_0_in;

out vec3 normal_cs; // cam (view) space
out vec3 normal_ws; // world space
out vec3 pos_cs;
out vec3 pos_ws;
out vec2 texture_coords;
#if USE_LOGARITHMIC_DEPTH_BUFFER
out float flogz;
#endif

uniform mat4 proj_matrix;
uniform mat4 model_matrix;
uniform mat4 view_matrix;
uniform mat4 normal_matrix;
uniform vec3 campos_ws;


void main()
{
	vec4 pos_ws_vec4 = model_matrix * vec4(position_in, 1.0);
	vec4 pos_cs_vec4 = view_matrix * pos_ws_vec4;
	gl_Position = proj_matrix * pos_cs_vec4;

	pos_ws = pos_ws_vec4.xyz;
	pos_cs = pos_cs_vec4.xyz;

	vec4 normal_ws_vec4 = normal_matrix * vec4(normal_in, 0.0);
	normal_ws = normal_ws_vec4.xyz;
	normal_cs = (view_matrix * normal_ws_vec4).xyz;

	texture_coords = texture_coords_0_in;

#if USE_LOGARITHMIC_DEPTH_BUFFER
	float farplane = 10000.0;
	float Fcoef = 2.0 / log2(farplane + 1.0);
	gl_Position.z = log2(max(1e-6, 1.0 + gl_Position.w)) * Fcoef - 1.0;

	flogz = 1.0 + gl_Position.w;
#endif
}

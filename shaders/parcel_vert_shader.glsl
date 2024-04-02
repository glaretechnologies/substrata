
in vec3 position_in;
in vec3 normal_in;
in vec2 texture_coords_0_in;

out vec3 pos_ws;
out vec2 texture_coords;

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

	texture_coords = texture_coords_0_in;
}

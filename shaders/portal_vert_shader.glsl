
in vec3 position_in;
in vec3 normal_in;
in vec2 texture_coords_0_in;

out vec3 pos_os;
out vec3 pos_cs;
out vec2 texture_coords;

layout (std140) uniform PerObjectVertUniforms
{
	PerObjectVertUniformsStruct per_object_data;
};


void main()
{
	vec4 pos_ws_vec4 = per_object_data.model_matrix * vec4(position_in, 1.0);
	vec4 pos_cs_vec4 = view_matrix * pos_ws_vec4;
	gl_Position = proj_matrix * pos_cs_vec4;

	pos_cs = pos_cs_vec4.xyz;
	pos_os = position_in;

	texture_coords = texture_coords_0_in;
}

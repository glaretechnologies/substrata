#version 150

in vec3 normal_cs; // cam (view) space
in vec3 normal_ws; // world space
in vec3 pos_cs;
in vec3 pos_ws;
in vec2 texture_coords;

uniform float time;
uniform vec3 colour;

out vec4 colour_out;


void main()
{
	float w = 0.05f;
	float edge_w = 0.02f;
	float t_x = fract(texture_coords.x * 40.0f);
	float t_y = fract(texture_coords.y * 40.0f);
	float alpha_x = smoothstep(0, edge_w, t_x) - smoothstep(edge_w + w, edge_w + w + edge_w, t_x);
	float alpha_y = smoothstep(0, edge_w, t_y) - smoothstep(edge_w + w, edge_w + w + edge_w, t_y);

	float main_w = 0.01f;
	float main_edge_w = 0.003f;
	float edge_alpha_x = 1 - /*step down around x = 0: */smoothstep(main_w, main_w + main_edge_w, texture_coords.x) + /*step up around x = 1: */smoothstep(1.0 - main_w - main_edge_w, 1.0 - main_w, texture_coords.x);
	float edge_alpha_y = 1 - /*step down around y = 0: */smoothstep(main_w, main_w + main_edge_w, texture_coords.y) + /*step up around y = 1: */smoothstep(1.0 - main_w - main_edge_w, 1.0 - main_w, texture_coords.y);

	float alpha = 1 - (1 - alpha_x) * (1 - alpha_y) * (1 - edge_alpha_x) * (1 - edge_alpha_y);
	colour_out = vec4(colour.x, colour.y, colour.z, alpha);
}

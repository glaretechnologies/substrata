#version 150

in vec3 normal_cs; // cam (view) space
in vec3 normal_ws; // world space
in vec3 pos_cs;
in vec3 pos_ws;
in vec2 texture_coords;

uniform float time;

out vec4 colour_out;


void main()
{
	float w = 0.05f;
	float edge_w = 0.02f;
	float t_x = fract(texture_coords.x * 40.0f);
	float t_y = fract(texture_coords.y * 40.0f);
	float alpha_x = smoothstep(0, edge_w, t_x) - smoothstep(edge_w + w, edge_w + w + edge_w, t_x);
	float alpha_y = smoothstep(0, edge_w, t_y) - smoothstep(edge_w + w, edge_w + w + edge_w, t_y);
	float alpha = 1 - (1 - alpha_x) * (1 - alpha_y);
	colour_out = vec4(44 / 256.f, 155 / 256.f, 32 / 256.f, alpha);
}

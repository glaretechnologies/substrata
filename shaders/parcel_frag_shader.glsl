
in vec3 pos_ws;
in vec2 texture_coords;

uniform float time;
uniform vec3 colour;

// Various outputs for order-independent transparency.
layout(location = 0) out vec4 transmittance_out;
layout(location = 1) out vec4 accum_out;


float gridFactor(float tiling, float w)
{
	float t_x = fract(texture_coords.x * tiling);
	float t_y = fract(texture_coords.y * tiling);

	float du_dx = abs(dFdx(texture_coords.x));
	float du_dy = abs(dFdy(texture_coords.x));

	float dv_dx = abs(dFdx(texture_coords.y));
	float dv_dy = abs(dFdy(texture_coords.y));

	float x_span_interval_space = max(du_dx, du_dy) * tiling;
	float y_span_interval_space = max(dv_dx, dv_dy) * tiling; // The larger the constant here, the more blurred the line is.  Smaller is sharper but too sharp gives aliasing.

	float t_x_start = t_x;
	float t_x_end   = t_x + x_span_interval_space;

	float t_y_start = t_y;
	float t_y_end   = t_y + y_span_interval_space;


	// Overlap is integral of line function from t_x_start to t_x_end, multiplied by w.
	float x_left_overlap;
	if(t_x_start < w)
	{
		float x_min = min(t_x_end, w);
		x_left_overlap = (x_min - t_x_start);// / w;
	}
	else
		x_left_overlap = 0.0;
	float y_left_overlap;
	if(t_y_start < w)
	{
		float y_min = min(t_y_end, w);
		y_left_overlap = (y_min - t_y_start);// / w;
	}
	else
		y_left_overlap = 0.0;
	//float x_min = min(t_x_end, w);
	//float x_left_overlap = max(0.f, 1 - t_x_start / w);
	//float y_left_overlap = max(0.f, 1 - t_y_start / w);

	// If t_x_end extends past 2, then there is at least 1 line completely between t_x_start and t_x_end
	float x_int_end = floor(t_x_end);
	float y_int_end = floor(t_y_end);
	float x_num_complete_intervals = max(0.f, x_int_end - 1.f);
	float y_num_complete_intervals = max(0.f, y_int_end - 1.f);

	float x_right_frac = t_x_end - x_int_end; // e [0, 1)
	float y_right_frac = t_y_end - y_int_end; // e [0, 1)

	float x_right_overlap = 0.0;
	if(x_int_end >= 1.f)
	{
		//if(right_frac < w)
		//	right_overlap = right_frac / w;
		//else
		//	right_overlap = 1;
		x_right_overlap = min(x_right_frac/* / w*/, w);
	}
	float y_right_overlap = 0.0;
	if(y_int_end >= 1.f)
		y_right_overlap = min(y_right_frac/* / w*/, w);

	float x_overlap = x_left_overlap + x_num_complete_intervals*w + x_right_overlap;
	float y_overlap = y_left_overlap + y_num_complete_intervals*w + y_right_overlap;

	float alpha_x = x_overlap / x_span_interval_space;
	float alpha_y = y_overlap / y_span_interval_space;

	return (1.0 - alpha_x) * (1.0 - alpha_y);
}

void main()
{
	const float tiling = 40.f;
	float w = 0.05f;
	float main_w = 0.01f;

	float small_grid_factor = gridFactor(tiling, w);
	float edge_grid_factor = gridFactor(1.f, main_w);

	float alpha = (1.0 - small_grid_factor * edge_grid_factor) * 1.1;
	accum_out = vec4(colour.x * alpha, colour.y * alpha, colour.z * alpha, alpha);

	float T = 1.0;
	transmittance_out = vec4(T, T, T, T);
}

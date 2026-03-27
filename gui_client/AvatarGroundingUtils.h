#pragma once

#include <graphics/AnimationData.h>
#include <cmath>


namespace AvatarGrounding
{
static constexpr float kDefaultAvatarEyeHeightM = 1.67f;
static constexpr float kHeadNodeToEyesOffsetM = 0.076f;
static constexpr float kDefaultToeBottomOffsetM = 0.0362269469f;
static constexpr float kRetargetedToeBottomOffsetM = 0.03f;


struct GroundingInfo
{
	float foot_bottom_height;
	float anchor_height_from_origin;
	float eye_height_above_ground;
};


inline bool tryGetNodeHeight(const AnimationData& animation_data, const char* node_name, bool use_retarget_adjustment, float& height_out)
{
	const int node_i = animation_data.getNodeIndex(node_name);
	if(node_i < 0)
		return false;

	const Vec4f node_pos = animation_data.getNodePositionModelSpace(node_i, use_retarget_adjustment);
	if(!std::isfinite(node_pos[1]))
		return false;

	height_out = node_pos[1];
	return true;
}


inline GroundingInfo computeGroundingInfo(const AnimationData& animation_data, bool use_retarget_adjustment, float toe_bottom_offset_m)
{
	GroundingInfo info = {};

	float toe_height = toe_bottom_offset_m;
	if(tryGetNodeHeight(animation_data, "LeftToe_End", use_retarget_adjustment, toe_height))
		info.foot_bottom_height = toe_height - toe_bottom_offset_m;
	else
		info.foot_bottom_height = 0.0f;

	float left_eye_height = 0.0f;
	float right_eye_height = 0.0f;
	float head_height = 0.0f;
	const bool has_left_eye = tryGetNodeHeight(animation_data, "LeftEye", use_retarget_adjustment, left_eye_height);
	const bool has_right_eye = tryGetNodeHeight(animation_data, "RightEye", use_retarget_adjustment, right_eye_height);

	if(has_left_eye && has_right_eye)
		info.anchor_height_from_origin = 0.5f * (left_eye_height + right_eye_height);
	else if(has_left_eye)
		info.anchor_height_from_origin = left_eye_height;
	else if(has_right_eye)
		info.anchor_height_from_origin = right_eye_height;
	else if(tryGetNodeHeight(animation_data, "Head", use_retarget_adjustment, head_height))
		info.anchor_height_from_origin = head_height + kHeadNodeToEyesOffsetM;
	else
		info.anchor_height_from_origin = info.foot_bottom_height + kDefaultAvatarEyeHeightM;

	info.eye_height_above_ground = info.anchor_height_from_origin - info.foot_bottom_height;
	if(!(std::isfinite(info.eye_height_above_ground) && (info.eye_height_above_ground > 0.1f)))
	{
		info.eye_height_above_ground = kDefaultAvatarEyeHeightM;
		info.anchor_height_from_origin = info.foot_bottom_height + info.eye_height_above_ground;
	}

	return info;
}
}

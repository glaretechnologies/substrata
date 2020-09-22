/*=====================================================================
AvatarGraphics.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:24:54 +1300
=====================================================================*/
#include "AvatarGraphics.h"


#include "opengl/OpenGLEngine.h"
#include "../dll/include/IndigoMesh.h"
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>


AvatarGraphics::AvatarGraphics()
{
	last_pos.set(0, 0, 0);
	last_selected_ob_target_pos.set(0, 0, 0);
}


AvatarGraphics::~AvatarGraphics()
{

}


/*
From https://en.wikipedia.org/wiki/Body_proportions#/media/File:Braus_1921_1.png:

2295 pixels high = 1.8m   = 1275 pixels/m
upper leg = 543px = 0.425m
lower leg = 579px = 0.454m

upper arm = 390px = 0.3m
lower arm = 350px = 0.274m

shoulder height below eye level
= 291px = 0.228m

Hip socket height below eye level
= 942px = 0.738m

ankle height below eye level
= 2070px = 1.623m.

shoulder half width 
= 240px = 0.188m


From https://www.youtube.com/watch?v=G8Veye-N0A4:

right ankle coords
frame     
128			9, 15
130			9.2	15			<- max extension
132			9, 15
134			8.8, 15.2
136			8.7		15.15
138			8.7	15.5
140			8.5	15.5
142			8.4	15.5
144			8.3	15.5
146			8
148			7.7
150			7.5
152			7.4
154			7
156			6.8
158			6.6
160			6.4
162			6.1
164			5.9
166			5.7
168			5.5
170			5.2
172			5.0
174			4.7
176			4.5
178			4.2
180			4.0
182			3.7		15.3
184			3.6		15.2
186			3.4		15.1
188			3.1		15.0
190			2.9		14.8
192			2.8		14.8
194			2.8		14.6
196			3		14.4
198			3		14.3
200			3.2		14.2		(90 deg foot rotation)
202			3.5		14.1
204			3.7		14.0
206			4.0		13.8
208			4.2		13.9
210			4.6		14.0
212			4.9		14
214			5.3		14.3
216			5.6		14.6
218			6		14.7
220			6.5		14.9
222			7		15
224			7.5		15.2
226			7.9		15.2
228			8.3		15.2
230			8.4		15.3
232			8.7		15.3
234			8.8		15.3		<-- max extension
236			8.8		15.3
238			8.6		15.4
240			8.5		15.5


*/
// Column 0 is x grid coord, column 1 is y grid coord, column 2 is estimated rotation of foot.
static const float ankle_data[] = {
/*130*/	8.85,		15.275,		 10, //Frame 130, max right ankle extension
/*132*/	8.9	,		15.3,		 5,
/*134*/	8.8	,		15.35,		 5,
/*136*/	8.7	,		15.4,		 4,
/*138*/	8.6	,		15.5,		 2,
/*140*/	8.5	,		15.5,		 0,
/*142*/	8.4	,		15.5,		 0,
/*144*/	8.25,		15.5,		 0,
/*146*/	8	,		15.5,		 0,
/*148*/	7.7	,		15.5,		 0,
/*150*/	7.45,		15.5,		 0,
/*152*/	7.2	,		15.5,		 0,
/*154*/	7	,		15.5,		 0,
/*156*/	6.8	,		15.5,		 0,
/*158*/	6.6	,		15.5,		 0,
/*160*/	6.35,		15.5,		 0,
/*162*/	6.1	,		15.5,		 0,
/*164*/	5.9	,		15.5,		 0,
/*166*/	5.7	,		15.5,		 0,
/*168*/	5.45,		15.5,		 0,
/*170*/	5.2	,		15.5,		 0,
/*172*/	4.95,		15.5,		 0,
/*174*/	4.7	,		15.5,		 0,
/*176*/	4.45,		15.5,		 0,
/*178*/	4.2	,		15.5,		 -2,
/*180*/	4	,		15.5,		 -4,
/*182*/	3.7	,		15.4,		 -8,
/*184*/	3.6	,		15.3,		 -10,
/*186*/	3.4	,		15.2,		 -15,
/*188*/	3.1	,		15.1,		-25,
/*190*/	2.9	,		14.9,		 -30,
/*192*/	2.8	,		14.75,		 -40,
/*194*/	2.87,		14.57,		 -50,
/*196*/	2.95,		14.44,		 -70,
/*198*/	3.07,		14.3,		 -80,
/*200*/	3.2	,		14.2,		 -95,		//(90 deg foot rotation)
/*202*/	3.4	,		14.1,		 -95,
/*204*/	3.7	,		14,			-95,
/*206*/	4	,		13.95,		 -90,
/*208*/	4.2	,		13.95,		 -80,
/*210*/	4.6	,		14,			-70,
/*212*/	4.97,		14.15,		 -60,
/*214*/	5.3	,		14.3,		 -50,
/*216*/	5.6	,		14.5,		 -40,
/*218*/	6	,		14.7,		 -25,
/*220*/	6.55,		14.9,		 -15,
/*222*/	7.1	,		15,			-10,
/*224*/	7.65,		15.1,		 -5,
/*226*/	8.1	,		15.17,		 0,
/*228*/	8.4	,		15.19,		 5,
/*230*/	8.6	,		15.22,		 10,
/*232*/	8.8	,		15.25,		 15
		//8.8,		15.3		//<--max extension

};


static const int NUM_FRAMES = 52;

static const Matrix4f rotateThenTranslateMatrix(const Vec3d& translation, const Vec3f& rotation)
{
	Matrix4f m;
	const float rot_len2 = rotation.length2();
	if(rot_len2 < 1.0e-20f)
		m.setToIdentity();
	else
	{
		const float rot_len = std::sqrt(rot_len2);
		m.setToRotationMatrix(rotation.toVec4fVector() / rot_len, rot_len);
	}
	m.setColumn(3, Vec4f((float)translation.x, (float)translation.y, (float)translation.z, 1.f));
	return m;
}


static const float upper_arm_length = 0.3f;
static const float lower_arm_length = 0.274f;

static const float upper_leg_length = 0.425f;
static const float upper_leg_radius = 0.06f;

static const float lower_leg_length = 0.454;
static const float lower_leg_radius = 0.04f;


//
//static float ankleHeightEnvelope(float x)
//{
//	//return myMax(0.05f, 0.15f - x*0.4f);
//	return myMax(0.05f, 0.1f - x* 0.4f + x*x*1.0f);
//}
//
//
//static float footRotationEnvelope(float x)
//{
//	return myMax(0.0f, 0.5f - x*6.0f);
//}


void AvatarGraphics::setOverallTransform(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, double cur_time)
{
	if(pos.getDist(last_pos) > 0.002)
	{
		setWalkAnimation(engine, pos, rotation, cur_time);
	}
	else
	{
		setStandAnimation(engine, pos, rotation, cur_time);
	}

	last_pos = pos;
	this->last_hand_pos = toVec3d(this->lower_arms[0].gl_ob->ob_to_world_matrix * Vec4f(0, 0, 1, 1)); // (0,0,1) in forearm cylinder object space is where the hand is.
}


void AvatarGraphics::setStandAnimation(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, double cur_time)
{
	const Matrix4f overall = rotateThenTranslateMatrix(pos, rotation);

	// Compute elevation angle to selected object.
	float target_angle = 0;
	if(selected_ob_beam.nonNull())
	{
		Matrix4f overall_inverse;
		overall.getInverseForAffine3Matrix(overall_inverse);
		const Vec4f to_target_os = overall_inverse * (last_selected_ob_target_pos - this->last_pos).toVec4fVector();
		target_angle = atan2(to_target_os[2], to_target_os[0]);
	}

	const float torso_offset = 0;

	const Vec4f hip_centre(0, 0, torso_offset -0.738f, 1);
	const Vec4f hip_centre_to_hip_0 = Vec4f(0, 1, 0, 0) * -0.10f;

	head.gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(0, 0, torso_offset) * head.base_transform;
	chest.gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(0, 0, torso_offset - 0.17f) * chest.base_transform;
	pelvis.gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(0, 0, torso_offset - 0.6f) * pelvis.base_transform;


	const float shoulder_dist = 0.188f;


	const float freq = 2.0f;
	const float phase_1 = 2.0f;
	float upper_arm_rot_angle_0 = (float)sin(freq * cur_time) * 0.04f;
	const float upper_arm_rot_angle_1 = (float)sin(freq * cur_time + phase_1) * 0.04f;

	// If the avatar has an object selected, raise the arm:
	if(selected_ob_beam.nonNull())
		upper_arm_rot_angle_0 = -target_angle + -0.6f + (float)sin(freq * cur_time           - 0.5) * 0.03f;

	const Vec4f shoulder_pos_0(0, -shoulder_dist, torso_offset + -0.228f, 1);
	const Vec4f shoulder_pos_1(0, shoulder_dist, torso_offset + -0.228f, 1);

	const Vec4f elbow_pos_0 = shoulder_pos_0 + Vec4f(-sin(upper_arm_rot_angle_0), 0, -cos(upper_arm_rot_angle_0), 0) * upper_arm_length;
	const Vec4f elbow_pos_1 = shoulder_pos_1 + Vec4f(-sin(upper_arm_rot_angle_1), 0, -cos(upper_arm_rot_angle_1), 0) * upper_arm_length;

	float lower_arm_rot_angle_0 = myMin(upper_arm_rot_angle_0, (float)sin(freq * cur_time           - 0.5) * 0.03f); // Don't rotate more than upper arm
	const float lower_arm_rot_angle_1 = myMin(upper_arm_rot_angle_1, (float)sin(freq * cur_time + phase_1 - 0.5) * 0.03f); // Don't rotate more than upper arm

	// If the avatar has an object selected, raise the arm:
	if(selected_ob_beam.nonNull())
		lower_arm_rot_angle_0 = -target_angle + -2.0f + (float)sin(freq * cur_time           - 0.5) * 0.03f;

	const Vec4f hip_pos_0 = hip_centre + hip_centre_to_hip_0;//(0, -0.13f, torso_offset -0.8f, 1);
	const Vec4f hip_pos_1 = hip_centre - hip_centre_to_hip_0;// (0, 0.13f, torso_offset -0.8f, 1);

	const Vec4f knee_pos_0 = hip_pos_0 + Vec4f(0, 0, -upper_leg_length, 0);
	const Vec4f knee_pos_1 = hip_pos_1 + Vec4f(0, 0, -upper_leg_length, 0);

	const Vec4f ankle_pos_0 = knee_pos_0 + Vec4f(0, 0, -lower_leg_length, 0);
	const Vec4f ankle_pos_1 = knee_pos_1 + Vec4f(0, 0, -lower_leg_length, 0);

	//upper_arms[0]->ob_to_world_matrix = overall * Matrix4f::translationMatrix(shoulder_pos_0);
	//upper_arms[1]->ob_to_world_matrix = overall * Matrix4f::translationMatrix(shoulder_pos_1);
	//
	//lower_arms[0]->ob_to_world_matrix = overall * Matrix4f::translationMatrix(elbow_pos_0);
	//lower_arms[1]->ob_to_world_matrix = overall * Matrix4f::translationMatrix(elbow_pos_1);

	upper_arms[0].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(shoulder_pos_0) * Matrix4f::rotationAroundYAxis(upper_arm_rot_angle_0) * upper_arms[0].base_transform;
	upper_arms[1].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(shoulder_pos_1) * Matrix4f::rotationAroundYAxis(upper_arm_rot_angle_1) * upper_arms[1].base_transform;

	lower_arms[0].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(elbow_pos_0) * Matrix4f::rotationAroundYAxis(lower_arm_rot_angle_0) * lower_arms[0].base_transform;
	lower_arms[1].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(elbow_pos_1) * Matrix4f::rotationAroundYAxis(lower_arm_rot_angle_1) * lower_arms[1].base_transform;

	upper_legs[0].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(hip_pos_0) * upper_legs[0].base_transform;// * Matrix4f::rotationMatrix(Vec4f(0, 1, 0, 0), upp
	upper_legs[1].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(hip_pos_1) * upper_legs[1].base_transform;// * Matrix4f::rotationMatrix(Vec4f(0, 1, 0, 0), upp

	lower_legs[0].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(knee_pos_0) * lower_legs[0].base_transform; //Matrix4f::translationMatrix(0, -0.13f, -1.3f);
	lower_legs[1].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(knee_pos_1) * lower_legs[1].base_transform; //Matrix4f::translationMatrix(0,  0.13f, -1.3f);

	const float foot_downwards_trans = 0.045f;
	feet[0].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(ankle_pos_0) * Matrix4f::translationMatrix(0, 0, -foot_downwards_trans) * feet[0].base_transform;
	feet[1].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(ankle_pos_1) * Matrix4f::translationMatrix(0, 0, -foot_downwards_trans) * feet[1].base_transform;

	engine.updateObjectTransformData(*upper_arms[0].gl_ob);
	engine.updateObjectTransformData(*upper_arms[1].gl_ob);
	engine.updateObjectTransformData(*lower_arms[0].gl_ob);
	engine.updateObjectTransformData(*lower_arms[1].gl_ob);
	engine.updateObjectTransformData(*upper_legs[0].gl_ob);
	engine.updateObjectTransformData(*upper_legs[1].gl_ob);
	engine.updateObjectTransformData(*lower_legs[0].gl_ob);
	engine.updateObjectTransformData(*lower_legs[1].gl_ob);
	engine.updateObjectTransformData(*feet[0].gl_ob);
	engine.updateObjectTransformData(*feet[1].gl_ob);
	engine.updateObjectTransformData(*chest.gl_ob);
	engine.updateObjectTransformData(*pelvis.gl_ob);
	engine.updateObjectTransformData(*head.gl_ob);
}


void AvatarGraphics::setWalkAnimation(OpenGLEngine& engine, const Vec3d& pos, const Vec3f& rotation, double cur_time)
{
	const Matrix4f overall = rotateThenTranslateMatrix(pos, rotation);

	// Compute elevation angle to selected object.
	float target_angle = 0;
	if(selected_ob_beam.nonNull())
	{
		Matrix4f overall_inverse;
		overall.getInverseForAffine3Matrix(overall_inverse);
		const Vec4f to_target_os = overall_inverse * (last_selected_ob_target_pos - this->last_pos).toVec4fVector();
		target_angle = atan2(to_target_os[2], to_target_os[0]);
	}

	//const float upper_leg_angle_0 = (float)sin(3.0 * cur_time) * 0.3f;
	//const float upper_leg_angle_1 = (float)sin(3.0 * cur_time + Maths::pi<double>()) * 0.3f;

	const float freq = 7.0f;

	const float torso_offset = (float)sin(freq * 2 * cur_time + Maths::pi_2<float>()) * 0.015f;

	// forwards = x
	const float step_amplitude = 0.35f;
	const float foot_pos_half_width = 0.09f;
	
	const float forwards_0 = (float)sin(freq * cur_time) * step_amplitude;
	//const float height_0 = -1.623f + myMax(0.f, (float)cos(freq * cur_time) * ankleHeightEnvelope(forwards_0)/*0.1f*/);
	//const Vec4f ankle_pos_0 = Vec4f(forwards_0, -foot_pos_half_width, height_0, 1.f);

	Vec4f ankle_pos_0;
	float foot_rot_angle_0;
	{
		const float phase = (float)cur_time * freq / Maths::get2Pi<float>() + 0.75f;
		const int index = myClamp<int>((int)(Maths::fract(phase) * NUM_FRAMES), 0, NUM_FRAMES-1);
		const int index_1 = (index == NUM_FRAMES-1) ? 0 : index + 1;
		const float t = (Maths::fract(phase) * NUM_FRAMES) - index;
		const float anim_x = Maths::lerp(ankle_data[index*3 + 0], ankle_data[index_1*3 + 0], t);
		const float anim_y = Maths::lerp(ankle_data[index*3 + 1], ankle_data[index_1*3 + 1], t);
		const float anim_z = Maths::lerp(ankle_data[index*3 + 2], ankle_data[index_1*3 + 2], t);
		const float xpos = anim_x * 0.1f - 0.6f;
		const float ypos = anim_y * -0.1f - 0.02f;
		foot_rot_angle_0 = degreeToRad(anim_z);
		ankle_pos_0 = Vec4f(xpos, -foot_pos_half_width, ypos, 1.f);
	}

	Vec4f ankle_pos_1;
	float foot_rot_angle_1;
	{
		const float phase = (float)cur_time * freq / Maths::get2Pi<float>() + 0.75f + 0.5f;
		const int index = myClamp<int>((int)(Maths::fract(phase) * NUM_FRAMES), 0, NUM_FRAMES-1);
		//printVar(130 + index*2);
		const int index_1 = (index == NUM_FRAMES-1) ? 0 : index + 1;
		const float t = (Maths::fract(phase) * NUM_FRAMES) - index;
		const float anim_x = Maths::lerp(ankle_data[index*3 + 0], ankle_data[index_1*3 + 0], t);
		const float anim_y = Maths::lerp(ankle_data[index*3 + 1], ankle_data[index_1*3 + 1], t);
		const float anim_z = Maths::lerp(ankle_data[index*3 + 2], ankle_data[index_1*3 + 2], t);
		const float xpos = anim_x * 0.1f - 0.6f;
		const float ypos = anim_y * -0.1f - 0.02f;
		foot_rot_angle_1 = degreeToRad(anim_z);
		ankle_pos_1 = Vec4f(xpos, foot_pos_half_width, ypos, 1.f);
	}

	const float phase_1 = Maths::pi<float>();
	const float forwards_1 = (float)sin(freq * cur_time + phase_1) * step_amplitude;
	//const float height_1 = -1.623f + myMax(0.f, (float)cos(freq * cur_time + phase_1) * ankleHeightEnvelope(forwards_1)/*0.1f*/);
	//const Vec4f ankle_pos_1 = Vec4f(forwards_1, foot_pos_half_width, height_1, 1.f);


	//const float foot_rot_angle_0 = myMax(0.f, (float)cos(freq * cur_time          ) * footRotationEnvelope(forwards_0)/*0.1f*/);
	//const float foot_rot_angle_1 = myMax(0.f, (float)cos(freq * cur_time + phase_1) * footRotationEnvelope(forwards_1)/*0.1f*/);


	const float hip_rot_angle = (float)sin(freq * cur_time + Maths::pi_2<float>()) * 0.1f;

	const Vec4f hip_centre(0, 0, torso_offset -0.738f, 1);
	const Vec4f hip_centre_to_hip_0 = Vec4f(0, cos(hip_rot_angle), sin(hip_rot_angle), 0) * -0.10f;

	// Solve for knee positions
	const Vec4f forwards(1, 0, 0, 0);
	const Vec4f hip_pos_0 = hip_centre + hip_centre_to_hip_0;//(0, -0.13f, torso_offset -0.8f, 1);
	const Vec4f hip_pos_1 = hip_centre - hip_centre_to_hip_0;// (0, 0.13f, torso_offset -0.8f, 1);

	const float l_1 = upper_leg_length; // upper leg length
	const float l_2 = lower_leg_length;

	Vec4f knee_pos_0;
	float upper_leg_rot_angle_0;
	float lower_leg_rot_angle_0;
	{
		const Vec4f v = ankle_pos_0 - hip_pos_0; // hip-to-ankle vector
		const Vec4f unit_v = normalise(v);
		//const float a_a = l_1*l_1 - l_2*l_2 + a_
		const float a_1 = v.length()/2;
		Vec4f f = normalise(::removeComponentInDir(forwards, unit_v)); // Vector orthogonal to v but roughly in forwards direction for body.
		const float f_mag = sqrt(myMax(0.f, l_1*l_1 - a_1*a_1));
		knee_pos_0 = hip_pos_0 + unit_v*a_1 + f*f_mag;

		const Vec4f hip_to_knee = knee_pos_0 - hip_pos_0;
		//printVar(hip_to_knee.length());
		upper_leg_rot_angle_0 = -Maths::pi_2<float>() - atan2(hip_to_knee[2], hip_to_knee[0]);

		const Vec4f knee_to_ankle = ankle_pos_0 - knee_pos_0;
		lower_leg_rot_angle_0 = -Maths::pi_2<float>() - atan2(knee_to_ankle[2], knee_to_ankle[0]);
	}

	Vec4f knee_pos_1;
	float upper_leg_rot_angle_1;
	float lower_leg_rot_angle_1;
	{
		const Vec4f v = ankle_pos_1 - hip_pos_1;
		const Vec4f unit_v = normalise(v);
		//const float a_a = l_1*l_1 - l_2*l_2 + a_
		const float a_1 = v.length()/2;
		Vec4f f = normalise(::removeComponentInDir(forwards, unit_v));
		const float f_mag = sqrt(myMax(0.f, l_1*l_1 - a_1*a_1));
		knee_pos_1 = hip_pos_1 + unit_v*a_1 + f*f_mag;

		const Vec4f hip_to_knee = knee_pos_1 - hip_pos_1;
		upper_leg_rot_angle_1 = -Maths::pi_2<float>() - atan2(hip_to_knee[2], hip_to_knee[0]);

		const Vec4f knee_to_ankle = ankle_pos_1 - knee_pos_1;
		lower_leg_rot_angle_1 = -Maths::pi_2<float>() - atan2(knee_to_ankle[2], knee_to_ankle[0]);
	}

	const float shoulder_dist = 0.188f;

	      float upper_arm_rot_angle_0 = (float)sin(freq * cur_time          ) * 0.3f;
	const float upper_arm_rot_angle_1 = (float)sin(freq * cur_time + phase_1) * 0.3f;

	// If the avatar has an object selected, raise the arm:
	if(selected_ob_beam.nonNull())
		upper_arm_rot_angle_0 = -target_angle + -0.6f + (float)sin(freq * cur_time           - 0.5) * 0.03f;

	const Vec4f shoulder_pos_0(0, -shoulder_dist, torso_offset + -0.228f, 1);
	const Vec4f shoulder_pos_1(0,  shoulder_dist, torso_offset + -0.228f, 1);

	const Vec4f elbow_pos_0 = shoulder_pos_0 + Vec4f(-sin(upper_arm_rot_angle_0), 0, -cos(upper_arm_rot_angle_0), 0) * upper_arm_length;
	const Vec4f elbow_pos_1 = shoulder_pos_1 + Vec4f(-sin(upper_arm_rot_angle_1), 0, -cos(upper_arm_rot_angle_1), 0) * upper_arm_length;

	      float lower_arm_rot_angle_0 = myMin(upper_arm_rot_angle_0, (float)sin(freq * cur_time           - 0.5) * 0.45f); // Don't rotate more than upper arm
	const float lower_arm_rot_angle_1 = myMin(upper_arm_rot_angle_1, (float)sin(freq * cur_time + phase_1 - 0.5) * 0.45f); // Don't rotate more than upper arm

	// If the avatar has an object selected, raise the arm:
	if(selected_ob_beam.nonNull())
		lower_arm_rot_angle_0 = -target_angle + -2.0f + (float)sin(freq * cur_time           - 0.5) * 0.03f;
	


	head.gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(0, 0, torso_offset) * head.base_transform;
	chest.gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(0, 0, torso_offset - 0.17f) * chest.base_transform;
	pelvis.gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(0, 0, torso_offset - 0.6f) * Matrix4f::rotationAroundXAxis(hip_rot_angle) * pelvis.base_transform;

	

	upper_arms[0].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(shoulder_pos_0) * Matrix4f::rotationAroundYAxis(upper_arm_rot_angle_0) * upper_arms[0].base_transform;
	upper_arms[1].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(shoulder_pos_1) * Matrix4f::rotationAroundYAxis(upper_arm_rot_angle_1) * upper_arms[1].base_transform;

	lower_arms[0].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(elbow_pos_0) * Matrix4f::rotationAroundYAxis(lower_arm_rot_angle_0) * lower_arms[0].base_transform;
	lower_arms[1].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(elbow_pos_1) * Matrix4f::rotationAroundYAxis(lower_arm_rot_angle_1) * lower_arms[1].base_transform;

	upper_legs[0].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(hip_pos_0) * Matrix4f::rotationAroundYAxis(upper_leg_rot_angle_0) * upper_legs[0].base_transform;// * Matrix4f::rotationMatrix(Vec4f(0, 1, 0, 0), upper_leg_angle_0);
	upper_legs[1].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(hip_pos_1) * Matrix4f::rotationAroundYAxis(upper_leg_rot_angle_1) * upper_legs[1].base_transform;// * Matrix4f::rotationMatrix(Vec4f(0, 1, 0, 0), upper_leg_angle_1);

	lower_legs[0].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(knee_pos_0) * Matrix4f::rotationAroundYAxis(lower_leg_rot_angle_0) * lower_legs[0].base_transform; //Matrix4f::translationMatrix(0, -0.13f, -1.3f);
	lower_legs[1].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(knee_pos_1) * Matrix4f::rotationAroundYAxis(lower_leg_rot_angle_1) * lower_legs[1].base_transform; //Matrix4f::translationMatrix(0,  0.13f, -1.3f);

	const float foot_downwards_trans = 0.045f;
	feet[0].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(ankle_pos_0) * Matrix4f::rotationAroundYAxis(-foot_rot_angle_0) * Matrix4f::translationMatrix(0, 0, -foot_downwards_trans) * feet[0].base_transform; //Matrix4f::translationMatrix(0, -0.13f, -1.66f);
	feet[1].gl_ob->ob_to_world_matrix = overall * Matrix4f::translationMatrix(ankle_pos_1) * Matrix4f::rotationAroundYAxis(-foot_rot_angle_1) * Matrix4f::translationMatrix(0, 0, -foot_downwards_trans) * feet[1].base_transform; //Matrix4f::translationMatrix(0, 0.13f, -1.66f);
	

	engine.updateObjectTransformData(*upper_arms[0].gl_ob);
	engine.updateObjectTransformData(*upper_arms[1].gl_ob);
	engine.updateObjectTransformData(*lower_arms[0].gl_ob);
	engine.updateObjectTransformData(*lower_arms[1].gl_ob);
	engine.updateObjectTransformData(*upper_legs[0].gl_ob);
	engine.updateObjectTransformData(*upper_legs[1].gl_ob);
	engine.updateObjectTransformData(*lower_legs[0].gl_ob);
	engine.updateObjectTransformData(*lower_legs[1].gl_ob);
	engine.updateObjectTransformData(*feet[0].gl_ob);
	engine.updateObjectTransformData(*feet[1].gl_ob);
	engine.updateObjectTransformData(*chest.gl_ob);
	engine.updateObjectTransformData(*pelvis.gl_ob);
	engine.updateObjectTransformData(*head.gl_ob);
}


static Matrix4f cylinderTransform(const Vec4f& endpoint_a, const Vec4f& endpoint_b, float x_axis_scale, float y_axis_scale)
{
	const float len = endpoint_a.getDist(endpoint_b);
	const Vec3f dir(normalise(endpoint_b - endpoint_a));
	Matrix4f basis;
	basis.constructFromVector(normalise(endpoint_b - endpoint_a));

	return Matrix4f::translationMatrix(endpoint_a) * basis * Matrix4f::scaleMatrix(x_axis_scale, y_axis_scale, len);
}


void AvatarGraphics::create(OpenGLEngine& engine)
{
	OpenGLMaterial material;
	material.albedo_rgb = Colour3f(0.5f);

	const float upper_arm_radius = 0.04f;
	const float lower_arm_radius = 0.04f;

	const float chest_length = 0.3f;
	const float chest_radius = 0.16f;

	const float pelvis_length = 0.2f;
	const float pelvis_radius = 0.16f;

	const float head_length = 0.18f;
	const float head_radius = 0.1f;

	const float foot_length = 0.3f;
	const float foot_radius = 0.04f;

	Reference<OpenGLMeshRenderData> cylinder_mesh = engine.getCylinderMesh();

	// Make left arm
	upper_arms[0].gl_ob = new GLObject();
	upper_arms[0].base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(0, 0, -upper_arm_length, 1), upper_arm_radius, upper_arm_radius);
	upper_arms[0].gl_ob->ob_to_world_matrix = upper_arms[0].base_transform;
	upper_arms[0].gl_ob->mesh_data = cylinder_mesh;
	upper_arms[0].gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	
	engine.addObject(upper_arms[0].gl_ob);

	lower_arms[0].gl_ob = new GLObject();
	lower_arms[0].base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(0, 0, -lower_arm_length, 1), lower_arm_radius, lower_arm_radius);
	lower_arms[0].gl_ob->ob_to_world_matrix = lower_arms[0].base_transform;
	lower_arms[0].gl_ob->mesh_data = cylinder_mesh;
	lower_arms[0].gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(lower_arms[0].gl_ob);

	// Make right arm
	upper_arms[1].gl_ob = new GLObject();
	upper_arms[1].base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(0, 0, -upper_arm_length, 1), upper_arm_radius, upper_arm_radius);
	upper_arms[1].gl_ob->ob_to_world_matrix = upper_arms[1].base_transform;
	upper_arms[1].gl_ob->mesh_data = cylinder_mesh;
	upper_arms[1].gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(upper_arms[1].gl_ob);

	lower_arms[1].gl_ob = new GLObject();
	lower_arms[1].base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(0, 0, -lower_arm_length, 1), lower_arm_radius, lower_arm_radius);
	lower_arms[1].gl_ob->ob_to_world_matrix = lower_arms[1].base_transform;
	lower_arms[1].gl_ob->mesh_data = cylinder_mesh;
	lower_arms[1].gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(lower_arms[1].gl_ob);


	// Make left leg
	upper_legs[0].gl_ob = new GLObject();
	upper_legs[0].base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(0, 0, -upper_leg_length, 1), upper_leg_radius, upper_leg_radius);
	upper_legs[0].gl_ob->ob_to_world_matrix = upper_legs[0].base_transform;
	upper_legs[0].gl_ob->mesh_data = cylinder_mesh;
	upper_legs[0].gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(upper_legs[0].gl_ob);

	lower_legs[0].gl_ob = new GLObject();
	lower_legs[0].base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(0, 0, -lower_leg_length, 1), lower_leg_radius, lower_leg_radius);
	lower_legs[0].gl_ob->ob_to_world_matrix = lower_legs[0].base_transform;
	lower_legs[0].gl_ob->mesh_data = cylinder_mesh;
	lower_legs[0].gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(lower_legs[0].gl_ob);

	// Make right leg
	upper_legs[1].gl_ob = new GLObject();
	upper_legs[1].base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(0, 0, -upper_leg_length, 1), upper_leg_radius, upper_leg_radius);
	upper_legs[1].gl_ob->ob_to_world_matrix = upper_legs[1].base_transform;
	upper_legs[1].gl_ob->mesh_data = cylinder_mesh;
	upper_legs[1].gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(upper_legs[1].gl_ob);

	lower_legs[1].gl_ob = new GLObject();
	lower_legs[1].base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(0, 0, -lower_leg_length, 1), lower_leg_radius, lower_leg_radius);
	lower_legs[1].gl_ob->ob_to_world_matrix = lower_legs[1].base_transform;
	lower_legs[1].gl_ob->mesh_data = cylinder_mesh;
	lower_legs[1].gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(lower_legs[1].gl_ob);

	// Make chest
	chest.gl_ob = new GLObject();
	chest.base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(0, 0, -chest_length, 1), chest_radius, chest_radius * 0.7f);
	chest.gl_ob->ob_to_world_matrix = chest.base_transform;
	chest.gl_ob->mesh_data = cylinder_mesh;
	//Indigo::MeshRef mesh = new Indigo::Mesh();
	//Indigo::Mesh::readFromFile("D:\\downloads\\nickvatar\\mesh_12467433656257381352.igmesh", *mesh);
	//chest->mesh_data = engine.buildIndigoMesh(mesh, false);
	chest.gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	
	engine.addObject(chest.gl_ob);

	// Make pelvis
	pelvis.gl_ob = new GLObject();
	pelvis.base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(0, 0, -pelvis_length, 1), pelvis_radius, pelvis_radius * 0.7f);
	pelvis.gl_ob->ob_to_world_matrix = pelvis.base_transform;
	pelvis.gl_ob->mesh_data = cylinder_mesh;
	pelvis.gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(pelvis.gl_ob);

	// Make head
	head.gl_ob = new GLObject();
	head.base_transform = cylinderTransform(Vec4f(0, 0, head_length/2, 1), Vec4f(0, 0, -head_length/2, 1), head_radius, head_radius);
	head.gl_ob->ob_to_world_matrix = head.base_transform;
	head.gl_ob->mesh_data = cylinder_mesh;
	head.gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(head.gl_ob);

	// Feet
	feet[0].gl_ob = new GLObject();
	feet[0].base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(foot_length/2, 0, 0, 1), foot_radius * 0.6f, foot_radius);
	feet[0].gl_ob->ob_to_world_matrix = feet[0].base_transform;
	feet[0].gl_ob->mesh_data = cylinder_mesh;
	feet[0].gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(feet[0].gl_ob);

	feet[1].gl_ob = new GLObject();
	feet[1].base_transform = cylinderTransform(Vec4f(0, 0, 0, 1), Vec4f(foot_length/2, 0, 0, 1), foot_radius * 0.6f, foot_radius);
	feet[1].gl_ob->ob_to_world_matrix = feet[1].base_transform;
	feet[1].gl_ob->mesh_data = cylinder_mesh;
	feet[1].gl_ob->materials = std::vector<OpenGLMaterial>(1, material);
	engine.addObject(feet[1].gl_ob);
}


void AvatarGraphics::destroy(OpenGLEngine& engine)
{
	engine.removeObject(upper_arms[0].gl_ob);
	engine.removeObject(upper_arms[1].gl_ob);
	engine.removeObject(lower_arms[0].gl_ob);
	engine.removeObject(lower_arms[1].gl_ob);
	engine.removeObject(upper_legs[0].gl_ob);
	engine.removeObject(upper_legs[1].gl_ob);
	engine.removeObject(lower_legs[0].gl_ob);
	engine.removeObject(lower_legs[1].gl_ob);
	engine.removeObject(feet[0].gl_ob);
	engine.removeObject(feet[1].gl_ob);
	engine.removeObject(chest.gl_ob);
	engine.removeObject(pelvis.gl_ob);
	engine.removeObject(head.gl_ob);

	if(selected_ob_beam.nonNull())
		engine.removeObject(selected_ob_beam);
}


void AvatarGraphics::setSelectedObBeam(OpenGLEngine& engine, const Vec3d& target_pos) // create or update beam
{
	const Vec3d src_pos = last_hand_pos;
	this->last_selected_ob_target_pos = target_pos;

	Matrix4f dir_matrix; dir_matrix.constructFromVector(normalise((target_pos - src_pos).toVec4fVector()));
	Matrix4f scale_matrix = Matrix4f::scaleMatrix(/*radius=*/0.03f,/*radius=*/0.03f, (float)target_pos.getDist(src_pos));
	Matrix4f ob_to_world = Matrix4f::translationMatrix(src_pos.toVec4fPoint()) * dir_matrix * scale_matrix;

	if(selected_ob_beam.isNull())
	{
		selected_ob_beam = new GLObject();
		selected_ob_beam->ob_to_world_matrix = ob_to_world;
		selected_ob_beam->mesh_data = engine.getCylinderMesh();

		OpenGLMaterial material;
		material.albedo_rgb = Colour3f(0.5f, 0.2f, 0.2f);
		material.transparent = true;
		material.alpha = 0.2f;

		selected_ob_beam->materials = std::vector<OpenGLMaterial>(1, material);
		engine.addObject(selected_ob_beam);
	}
	else // Else if ob has already been created:
	{
		selected_ob_beam->ob_to_world_matrix = ob_to_world;
		engine.updateObjectTransformData(*selected_ob_beam);
	}
}


void AvatarGraphics::hideSelectedObBeam(OpenGLEngine& engine)
{
	if(selected_ob_beam.nonNull())
	{
		engine.removeObject(selected_ob_beam);
		selected_ob_beam = NULL;
	}
}

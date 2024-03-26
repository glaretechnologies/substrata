/*=====================================================================
CameraController.cpp
--------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "CameraController.h"


#include <maths/mathstypes.h>
#include <maths/GeometrySampling.h>
#include <maths/matrix3.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
#include <algorithm>
#include <math.h>


static const double cap = 1.0e-4;


CameraController::CameraController()
{
	base_move_speed   = 0.035;
	base_rotate_speed = 0.005;

	move_speed_scale = 1;
	mouse_sensitivity_scale = 1;

	invert_mouse = false;
	invert_sideways_movement = false;

	third_person = true;
	third_person_cam_position = Vec3d(0,0,2);
	current_third_person_target_pos = Vec3d(0,0,2);
	selfie_mode = false;

	//start_cam_rot_z = 0;
	//end_cam_rot_z = 0;
	//start_transition_time = -2;
	//end_transition_time = -1;

	third_person_cam_dist = 3.f;


	// NOTE: Call initialise after the member variables above have been initialised.
	initialise(Vec3d(0.0), Vec3d(0, 1, 0), Vec3d(0, 0, 1), 0.03, 0, 0);
}


CameraController::~CameraController()
{}


void CameraController::initialise(const Vec3d& cam_pos, const Vec3d& cam_forward, const Vec3d& cam_up, double lens_sensor_dist_, double lens_shift_up_, double lens_shift_right_)
{
	// Copy camera position and provided camera up vector
	position = cam_pos;
	initialised_up = cam_up;

	lens_sensor_dist = lens_sensor_dist_;
	lens_shift_up = lens_shift_up_;
	lens_shift_right = lens_shift_right_;

	// Construct basis
	Vec3d camera_up, camera_forward, camera_right;

	camera_forward = normalise(cam_forward);
	camera_up = getUpForForwards(cam_forward, initialised_up);
	camera_right = crossProduct(camera_forward, camera_up);

	rotation.x = atan2(cam_forward.y, cam_forward.x);
	rotation.y = acos(cam_forward.z / cam_forward.length());

	const Vec3d rollplane_x_basis = crossProduct(camera_forward, initialised_up);
	const Vec3d rollplane_y_basis = crossProduct(rollplane_x_basis, camera_forward);
	const double rollplane_x = dot(camera_right, rollplane_x_basis);
	const double rollplane_y = dot(camera_right, rollplane_y_basis);

	rotation.z = atan2(rollplane_y, rollplane_x);

	initial_rotation = rotation;
}


void CameraController::update(const Vec3d& pos_delta, const Vec2d& rot_delta)
{
	const double rotate_speed        = base_rotate_speed * mouse_sensitivity_scale;
	const double move_speed          = base_move_speed * mouse_sensitivity_scale * move_speed_scale;
	const double sideways_dir_factor = invert_sideways_movement ? -1.0 : 1.0;

	if(rot_delta.x != 0 || rot_delta.y != 0)
	{
		// Accumulate rotation angles, taking into account mouse speed and invertedness.
		rotation.x += rot_delta.y * -rotate_speed;
		rotation.y += rot_delta.x * -rotate_speed * (invert_mouse ? -1 : 1) * (selfie_mode ? -1 : 1);

		rotation.y = std::max(cap, std::min(Maths::pi<double>() - cap, rotation.y));
	}

	// Construct camera basis.
	const Vec3d forwards(sin(rotation.y) * cos(rotation.x),
		sin(rotation.y) * sin(rotation.x),
		cos(rotation.y));
	const Vec3d up = getUpForForwards(forwards, initialised_up);
	const Vec3d right = ::crossProduct(forwards, up);

	position += right		* pos_delta.x * move_speed * sideways_dir_factor +
				forwards	* pos_delta.y * move_speed +
				up			* pos_delta.z * move_speed * sideways_dir_factor;
}


Vec3d CameraController::getFirstPersonPosition() const
{
	return position;
}


Vec3d CameraController::getPosition() const
{
	return third_person ? third_person_cam_position : position;
}


// Set first person position to pos, Set a suitable third-person camera position set back from pos.
void CameraController::setFirstAndThirdPersonPositions(const Vec3d& pos)
{
	position = pos;


	current_third_person_target_pos = pos;

	// Use a default backwards vector to compute the third-person camera position.
	const Vec4f cam_back_dir = (getForwardsVec() * -getThirdPersonCamDist() + getUpVec() * 0.2).toVec4fVector();

	third_person_cam_position = current_third_person_target_pos + Vec3d(cam_back_dir);
}


void CameraController::setFirstPersonPosition(const Vec3d& pos)
{
	position = pos;
}


void CameraController::setMouseSensitivity(double sensitivity)
{
	const double speed_base = (1 + std::sqrt(5.0)) * 0.5;
	mouse_sensitivity_scale = (float)pow(speed_base, sensitivity);
}


void CameraController::setMoveScale(double move_scale)
{
	move_speed_scale = move_scale;
}


void CameraController::getBasis(Vec3d& right_out, Vec3d& up_out, Vec3d& forward_out) const
{
	Vec3d use_rotation = rotation;
	if(selfie_mode)
	{
		use_rotation.x += Maths::pi<double>();
		use_rotation.y = Maths::pi<double>() - rotation.y;
	}

	getBasisForAngles(use_rotation, initialised_up, right_out, up_out, forward_out);
}


Vec4f CameraController::vectorToCamSpace(const Vec4f& v) const
{
	const Vec4f forwards = getForwardsVec().toVec4fVector();
	const Vec4f right    = getRightVec().toVec4fVector();
	const Vec4f up       = getUpVec().toVec4fVector();

	return Vec4f(dot(v, right), dot(v, forwards), dot(v, up), 0.f);
}


Vec3d CameraController::getAngles() const
{
	return rotation;
}


void CameraController::setAngles(const Vec3d& newangles)
{
	rotation = newangles;
}


void CameraController::resetRotation()
{
	rotation = initial_rotation;
}


Vec3d CameraController::getForwardsVec() const
{
	return Vec3d(
		sin(rotation.y) * cos(rotation.x),
		sin(rotation.y) * sin(rotation.x),
		cos(rotation.y)
	);
}


Vec3d CameraController::getRightVec() const
{
	return normalise(crossProduct(getForwardsVec(), Vec3d(0,0,1)));
}


Vec3d CameraController::getUpVec() const
{
	return crossProduct(getRightVec(), getForwardsVec());
}


Vec3d CameraController::getUpForForwards(const Vec3d& forwards, const Vec3d& singular_up)
{
	Vec3d up_out;
	Vec3d world_up = Vec3d(0, 0, 1);

	if(absDot(world_up, forwards) == 1) // If we are exactly singular then use the provided 
	{
		up_out = singular_up;
	}
	else
	{
		up_out = world_up;
		up_out.removeComponentInDir(forwards);
	}

	return normalise(up_out);
}


void CameraController::getBasisForAngles(const Vec3d& angles_in, const Vec3d& singular_up, Vec3d& right_out, Vec3d& up_out, Vec3d& forward_out)
{
	// Get un-rolled basis
	forward_out = Vec3d(
		sin(angles_in.y) * cos(angles_in.x),
		sin(angles_in.y) * sin(angles_in.x),
		cos(angles_in.y));
	up_out = getUpForForwards(forward_out, singular_up);
	right_out = ::crossProduct(forward_out, up_out);

	// Apply camera roll
	const Matrix3d roll_basis = Matrix3d::rotationMatrix(forward_out, angles_in.z) * Matrix3d(right_out, forward_out, up_out);
	right_out	= roll_basis.getColumn0();
	forward_out	= roll_basis.getColumn1();
	up_out		= roll_basis.getColumn2();
}


// Returns true if we are in third person view and have zoomed in sufficiently far to change to first person view.
bool CameraController::handleScrollWheelEvent(float delta_y)
{
	if(thirdPersonEnabled())
	{
		const float MIN_CAM_DIST = 0.5f;

		// Make change proportional to distance value.
		// Mouse wheel scroll up reduces distance.
		third_person_cam_dist = myClamp<float>(third_person_cam_dist - (third_person_cam_dist * delta_y * 0.002f), MIN_CAM_DIST, 20.f);

		return third_person_cam_dist == MIN_CAM_DIST; // We have zoomed in all the way.
	}
	else
		return false;
}


void CameraController::setSelfieModeEnabled(double cur_time, bool enabled)
{
	selfie_mode = enabled;

	//start_transition_time = cur_time;
	//end_transition_time = cur_time + 1;
}


#if BUILD_TESTS

#include "../maths/mathstypes.h"
#include "../utils/TestUtils.h"
#include "../utils/ConPrint.h"

void CameraController::test()
{
	CameraController cc;
	Vec3d r, f, u, angles;


	// Initialise canonical viewing system - camera at origin, looking along y+ with z+ up
	cc.initialise(Vec3d(0.0), Vec3d(0, 1, 0), Vec3d(0, 0, 1), 0.03, 0, 0);
	cc.getBasis(r, u, f);
	testAssert(::epsEqual(r.x, 1.0)); testAssert(::epsEqual(r.y, 0.0)); testAssert(::epsEqual(r.z, 0.0));
	testAssert(::epsEqual(f.x, 0.0)); testAssert(::epsEqual(f.y, 1.0)); testAssert(::epsEqual(f.z, 0.0));
	testAssert(::epsEqual(u.x, 0.0)); testAssert(::epsEqual(u.y, 0.0)); testAssert(::epsEqual(u.z, 1.0));


	// Initialise camera to look down along z-, with y+ up
	cc.initialise(Vec3d(0.0), Vec3d(0, 0, -1), Vec3d(0, 1, 0), 0.03, 0, 0);
	cc.getBasis(r, u, f);
	testAssert(::epsEqual(r.x, 1.0)); testAssert(::epsEqual(r.y, 0.0)); testAssert(::epsEqual(r.z,  0.0));
	testAssert(::epsEqual(f.x, 0.0)); testAssert(::epsEqual(f.y, 0.0)); testAssert(::epsEqual(f.z, -1.0));
	testAssert(::epsEqual(u.x, 0.0)); testAssert(::epsEqual(u.y, 1.0)); testAssert(::epsEqual(u.z,  0.0));


	// Initialise canonical viewing system and test that the viewing angles are correct
	cc.initialise(Vec3d(0.0), Vec3d(0, 0, -1), Vec3d(0, 1, 0), 0.03, 0, 0);
	angles = cc.getAngles();
	testAssert(::epsEqual(angles.x, 0.0)); testAssert(::epsEqual(angles.y, NICKMATHS_PI)); testAssert(::epsEqual(angles.z,  0.0));

	// Apply a rotation along z (roll) of 90 degrees, or pi/4 radians
	angles.z = -NICKMATHS_PI_2;
	CameraController::getBasisForAngles(angles, Vec3d(0, 1, 0), r, u, f);
}

#endif // BUILD_TESTS

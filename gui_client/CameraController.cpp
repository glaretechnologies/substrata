/*=====================================================================
CameraController.cpp
--------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "CameraController.h"


#include <maths/mathstypes.h>
#include <maths/GeometrySampling.h>
#include <maths/matrix3.h>
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>
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

	current_cam_mode = CameraMode_Standard;

	target_ob_to_world_matrix = Matrix4f::identity();

	//start_cam_rot_z = 0;
	//end_cam_rot_z = 0;
	//start_transition_time = -2;
	//end_transition_time = -1;

	target_is_vehicle = false;

	third_person_cam_dist = 3.f;

	free_cam_desired_vel = Vec3d(0.0);
	free_cam_vel = Vec3d(0.0);


	// NOTE: Call initialise after the member variables above have been initialised.
	initialise(Vec3d(0.0), Vec3d(0, 1, 0), Vec3d(0, 0, 1), 0.03, 0, 0);
}


CameraController::~CameraController()
{}


Vec3d CameraController::getRotationAnglesFromCameraBasis(const Vec3d& cam_forward, const Vec3d& camera_right)
{
	assert(cam_forward.isUnitLength());
	assert(camera_right.isUnitLength());

	Vec3d rotation;
	rotation.x = atan2(cam_forward.y, cam_forward.x);
	rotation.y = acos(cam_forward.z / cam_forward.length());

	const Vec3d up(0,0,1);

	const Vec3d rollplane_x_basis = crossProduct(cam_forward, /*initialised_up*/up);
	const Vec3d rollplane_y_basis = crossProduct(rollplane_x_basis, cam_forward);
	const double rollplane_x = dot(camera_right, rollplane_x_basis);
	const double rollplane_y = dot(camera_right, rollplane_y_basis);

	rotation.z = atan2(rollplane_y, rollplane_x);

	return rotation;
}


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

	rotation = getRotationAnglesFromCameraBasis(camera_forward, camera_right);

	last_avatar_rotation = rotation;
}


void CameraController::updateRotation(double pitch_delta, double heading_delta)
{
	const double rotate_speed        = base_rotate_speed * mouse_sensitivity_scale;

	if(pitch_delta != 0 || heading_delta != 0)
	{
		// Accumulate rotation angles, taking into account mouse speed and invertedness.
		rotation.x += heading_delta * -rotate_speed;

		const double new_unclamped_rot_y = rotation.y + pitch_delta * -rotate_speed * (invert_mouse ? -1 : 1) * (selfie_mode ? -1 : 1);
		rotation.y = myClamp(new_unclamped_rot_y, cap, Maths::pi<double>() - cap);
	}
}


void CameraController::setTargetObjectTransform(const Matrix4f& ob_to_world_matrix, bool target_is_vehicle_)
{
	this->target_ob_to_world_matrix = ob_to_world_matrix;
	this->target_is_vehicle = target_is_vehicle_;
}


Vec3d CameraController::getFirstPersonPosition() const
{
	return position;
}


static Vec3d dirForAngles(double phi, double theta)
{
	return Vec3d(
		sin(theta) * cos(phi),
		sin(theta) * sin(phi),
		cos(theta)
	);
}


Matrix4f CameraController::getFixedAngleWorldToCamRotationMatrix() const
{
	// Bit of a hack: vehicle object coordinates are usually y-up, z-forwards and x-right, whereas avatar coords are
	// z-up and x-right.
	const Matrix4f cam_to_ob = target_is_vehicle ? 
		Matrix4f::rotationAroundYAxis((float)rotation.x) * Matrix4f::rotationAroundXAxis((float)-rotation.y) :
		Matrix4f::rotationAroundZAxis((float)rotation.x + Maths::pi_2<float>()) * Matrix4f::rotationAroundXAxis(-(float)rotation.y + Maths::pi_2<float>());

	Matrix4f cam_to_world = target_ob_to_world_matrix * cam_to_ob;

	cam_to_world.setColumn(3, Vec4f(0,0,0,1));

	Matrix4f cam_to_world_rot, rest;
	if(!cam_to_world.polarDecomposition(cam_to_world_rot, rest))
		return Matrix4f::identity();

	return cam_to_world_rot.getTranspose();
}


Vec3d CameraController::fixedAngleCameraDir() const
{
	const Matrix4f world_to_cam_rot = getFixedAngleWorldToCamRotationMatrix();

	return Vec3d(normalise(world_to_cam_rot.getRow(1)));
}


Vec3d CameraController::getPosition() const
{
	if(current_cam_mode == CameraMode_Standard)
	{
		return third_person ? third_person_cam_position : position;
	}
	else if(current_cam_mode == CameraMode_FixedAngle)
	{
		const Vec3d target_position = toVec3d(target_ob_to_world_matrix * Vec4f(0,0,0,1));
		const Vec3d dir = fixedAngleCameraDir();
		return target_position - dir * third_person_cam_dist;
	}
	else if(current_cam_mode == CameraMode_FreeCamera || current_cam_mode == CameraMode_TrackingCamera)
	{
		return third_person_cam_position;
	}
	else
	{
		assert(0);
		return Vec3d(0.0);
	}
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


void CameraController::setThirdPersonCamPosition(const Vec3d& pos)
{
	third_person_cam_position = pos;
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


void CameraController::getWorldToCameraMatrix(Matrix4f& world_to_camera_matrix_out)
{
	const Vec3d cam_pos = getPosition();

	if(current_cam_mode == CameraMode_Standard || current_cam_mode == CameraMode_FreeCamera)
	{
		Vec3d use_rotation = rotation;
		if(selfie_mode)
		{
			use_rotation.x += Maths::pi<double>();
			use_rotation.y = Maths::pi<double>() - rotation.y;
		}

		Vec3d right, up, forwards;
		getBasisForAngles(use_rotation, initialised_up, right, up, forwards);

		const Matrix4f to_camera_rot = Matrix4f::fromRows(right.toVec4fVector(), forwards.toVec4fVector(), up.toVec4fVector(), Vec4f(0,0,0,1));
		to_camera_rot.rightMultiplyWithTranslationMatrix(-cam_pos.toVec4fVector(), /*result=*/world_to_camera_matrix_out);
	}
	else if(current_cam_mode == CameraMode_FixedAngle)
	{
		const Matrix4f world_to_cam_rot = getFixedAngleWorldToCamRotationMatrix();

		world_to_cam_rot.rightMultiplyWithTranslationMatrix(-cam_pos.toVec4fVector(), /*result=*/world_to_camera_matrix_out);
	}
	else if(current_cam_mode == CameraMode_TrackingCamera)
	{
		const Vec4f target_pos = target_ob_to_world_matrix * Vec4f(0,0,0,1);
		const Vec4f cam_to_target = target_pos - third_person_cam_position.toVec4fPoint();

		const Vec4f forward = normalise(cam_to_target);
		const Vec4f up      = normalise(removeComponentInDir(Vec4f(0,0,1,0), normalise(cam_to_target)));
		const Vec4f right   = crossProduct(forward, up);

		const Matrix4f to_camera_rot = Matrix4f::fromRows(right, forward, up, Vec4f(0,0,0,1));
		to_camera_rot.rightMultiplyWithTranslationMatrix(-cam_pos.toVec4fVector(), /*result=*/world_to_camera_matrix_out);
	}
}


Vec4f CameraController::vectorToCamSpace(const Vec4f& v) const
{
	const Vec4f forwards = getForwardsVec().toVec4fVector();
	const Vec4f right    = getRightVec().toVec4fVector();
	const Vec4f up       = getUpVec().toVec4fVector();

	return Vec4f(dot(v, right), dot(v, forwards), dot(v, up), 0.f);
}


Vec3d CameraController::getAvatarAngles()
{
	if(current_cam_mode == CameraMode_Standard)
	{
		return rotation;
	}
	else if(current_cam_mode == CameraMode_FixedAngle || current_cam_mode == CameraMode_FreeCamera || current_cam_mode == CameraMode_TrackingCamera)
	{
		return last_avatar_rotation;
	}
	else
	{
		assert(0);
		return Vec3d(0.0);
	}
}


Vec3d CameraController::getAngles() const
{
	if(current_cam_mode == CameraMode_TrackingCamera)
	{
		const Vec4f target_pos = target_ob_to_world_matrix * Vec4f(0,0,0,1);
		const Vec4f cam_to_target = normalise(target_pos - third_person_cam_position.toVec4fPoint());
		Vec3d rot;
		rot.x = atan2(cam_to_target[1], cam_to_target[0]);
		rot.y = acos(cam_to_target[2]);
		rot.z = 0;
		return rot;
	}
	else if(current_cam_mode == CameraMode_FixedAngle)
	{
		const Matrix4f cam_to_world = getFixedAngleWorldToCamRotationMatrix().getTranspose();
		const Vec4f forwards = cam_to_world * Vec4f(0,1,0,0);
		const Vec4f right = cam_to_world * Vec4f(1,0,0,0);
		
		Vec3d rot;
		rot = getRotationAnglesFromCameraBasis(toVec3d(forwards), toVec3d(right));
		return rot;
	}
	else
	{
		return rotation;
	}
}


void CameraController::setAngles(const Vec3d& newangles)
{
	rotation = newangles;
}


Vec3d CameraController::getForwardsMoveVec() const
{
	if(current_cam_mode == CameraMode_Standard || current_cam_mode == CameraMode_FreeCamera)
	{
		return dirForAngles(rotation.x, rotation.y);
	}
	else if(current_cam_mode == CameraMode_FixedAngle || current_cam_mode == CameraMode_TrackingCamera)
	{
		return dirForAngles(last_avatar_rotation.x, last_avatar_rotation.y);
	}
	else
	{
		assert(0);
		return Vec3d(0.0);
	}
}


Vec3d CameraController::getRightMoveVec() const
{
	return normalise(crossProduct(getForwardsMoveVec(), Vec3d(0,0,1)));
}


Vec3d CameraController::getForwardsVec() const
{
	if(current_cam_mode == CameraMode_Standard || current_cam_mode == CameraMode_FreeCamera)
	{
		return dirForAngles(rotation.x, rotation.y);
	}
	else if(current_cam_mode == CameraMode_FixedAngle)
	{
		return fixedAngleCameraDir();
	}
	else
	{
		const Vec4f target_pos = target_ob_to_world_matrix * Vec4f(0,0,0,1);
		const Vec4f cam_to_target = target_pos - third_person_cam_position.toVec4fPoint();
		return Vec3d(normalise(cam_to_target));
	}
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
	forward_out = dirForAngles(angles_in.x, angles_in.y);
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


void CameraController::standardCameraModeSelected()
{
	setStateBeforeCameraModeChange();

	current_cam_mode = CameraMode_Standard;
}


void CameraController::fixedAngleCameraModeSelected()
{
	setStateBeforeCameraModeChange();

	current_cam_mode = CameraMode_FixedAngle;
}


void CameraController::freeCameraModeSelected()
{
	setStateBeforeCameraModeChange();

	current_cam_mode = CameraMode_FreeCamera;
}


void CameraController::trackingCameraModeSelected()
{
	setStateBeforeCameraModeChange();

	current_cam_mode = CameraMode_TrackingCamera;
}


void CameraController::setStateBeforeCameraModeChange()
{
	if(current_cam_mode == CameraMode_TrackingCamera)
	{
		// Update rotation
		const Vec4f target_pos = target_ob_to_world_matrix * Vec4f(0,0,0,1);
		const Vec4f cam_to_target = normalise(target_pos - third_person_cam_position.toVec4fPoint());
		rotation.x = atan2(cam_to_target[1], cam_to_target[0]);
		rotation.y = acos(cam_to_target[2]);
		rotation.z = 0;
	}
	else if(current_cam_mode == CameraMode_FixedAngle)
	{
		third_person_cam_position = getPosition();

		const Matrix4f cam_to_world = getFixedAngleWorldToCamRotationMatrix().getTranspose();
		const Vec4f forwards = cam_to_world * Vec4f(0,1,0,0);
		const Vec4f right = cam_to_world * Vec4f(1,0,0,0);
		
		rotation = getRotationAnglesFromCameraBasis(toVec3d(forwards), toVec3d(right));
		rotation.z = 0; // We don't really handle non-zero rolls in most camera modes.
	}

	if(current_cam_mode == CameraMode_Standard)
	{
		this->last_avatar_rotation = rotation;
	}
}


void CameraController::setFreeCamMovementDesiredVel(const Vec3f& vel)
{
	free_cam_desired_vel = toVec3d(vel);
}


void CameraController::think(double dt)
{
	const double damping_factor = (free_cam_desired_vel.length() > 0) ? 0.0 : 2.0;
	free_cam_vel = free_cam_vel * myMax(0.0, (1.0 - dt * damping_factor)) + free_cam_desired_vel * dt * 5.0;

	if(current_cam_mode == CameraMode_FreeCamera)
	{
		third_person_cam_position += free_cam_vel * dt;
	}
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
//	cc.initialise(Vec3d(0.0), Vec3d(0, 1, 0), Vec3d(0, 0, 1), 0.03, 0, 0);
//	cc.getBasis(r, u, f);
//	testAssert(::epsEqual(r.x, 1.0)); testAssert(::epsEqual(r.y, 0.0)); testAssert(::epsEqual(r.z, 0.0));
//	testAssert(::epsEqual(f.x, 0.0)); testAssert(::epsEqual(f.y, 1.0)); testAssert(::epsEqual(f.z, 0.0));
//	testAssert(::epsEqual(u.x, 0.0)); testAssert(::epsEqual(u.y, 0.0)); testAssert(::epsEqual(u.z, 1.0));
//
//
//	// Initialise camera to look down along z-, with y+ up
//	cc.initialise(Vec3d(0.0), Vec3d(0, 0, -1), Vec3d(0, 1, 0), 0.03, 0, 0);
//	cc.getBasis(r, u, f);
//	testAssert(::epsEqual(r.x, 1.0)); testAssert(::epsEqual(r.y, 0.0)); testAssert(::epsEqual(r.z,  0.0));
//	testAssert(::epsEqual(f.x, 0.0)); testAssert(::epsEqual(f.y, 0.0)); testAssert(::epsEqual(f.z, -1.0));
//	testAssert(::epsEqual(u.x, 0.0)); testAssert(::epsEqual(u.y, 1.0)); testAssert(::epsEqual(u.z,  0.0));


	// Initialise canonical viewing system and test that the viewing angles are correct
	cc.initialise(Vec3d(0.0), Vec3d(0, 0, -1), Vec3d(0, 1, 0), 0.03, 0, 0);
	angles = cc.getAngles();
	testAssert(::epsEqual(angles.x, 0.0)); testAssert(::epsEqual(angles.y, NICKMATHS_PI)); testAssert(::epsEqual(angles.z,  0.0));

	// Apply a rotation along z (roll) of 90 degrees, or pi/4 radians
	angles.z = -NICKMATHS_PI_2;
	CameraController::getBasisForAngles(angles, Vec3d(0, 1, 0), r, u, f);
}

#endif // BUILD_TESTS

/*=====================================================================
CameraController.h
------------------
Copyright Glare Technologies Limited 2010 -
Generated at Thu Dec 09 17:24:15 +1300 2010
=====================================================================*/
#pragma once


#include "../maths/vec3.h"
#include "../maths/vec2.h"
#include "../maths/matrix3.h"
#include "../maths/Quat.h"


/*=====================================================================
CameraController
----------------
Branched from Indigo before Yves' refactor made it a qt object with indigo stuff.
=====================================================================*/
class CameraController
{
public:
	CameraController();
	~CameraController();

	void initialise(const Vec3d& cam_pos, const Vec3d& cam_forwards, const Vec3d& cam_up, double lens_sensor_dist, double lens_shift_up, double lens_shift_right);

	void update(const Vec3d& pos_delta, const Vec2d& rot_delta);

	void updateTrackball(const Vec3d& pos_delta, const Vec2d& rot_delta);

	Vec3d getPosition() const;
	void setPosition(const Vec3d& pos);

	void setMouseSensitivity(double sensitivity);
	void setMoveScale(double move_scale); // Adjust camera movement speed based on world scale

	void getBasis(Vec3d& right_out, Vec3d& up_out, Vec3d& forward_out) const;

	Vec3d getAngles() const; // Specified as (heading, pitch, roll).
	void resetRotation();
	void setAngles(const Vec3d& newangles);

	Vec3d getForwardsVec() const;
	Vec3d getRightVec() const;
	Vec3d getUpVec() const;

	void setAllowPitching(bool allow_pitching);

	static Vec3d getUpForForwards(const Vec3d& forwards, const Vec3d& singular_up);
	static void getBasisForAngles(const Vec3d& angles_in, const Vec3d& singular_up, Vec3d& right_out, Vec3d& up_out, Vec3d& forward_out);

	static void getAxisAngleForAngles(const Vec3d& euler_angles_in, Vec3d& axis_out, double& angle_out);

	void setTargetPos(const Vec3d& p);

	bool invert_mouse;
	bool invert_sideways_movement;

	static void test();

private:

	Vec3d position;
	Vec3d rotation; // Specified as (heading, pitch, roll).
					// heading is phi: rotation angle in x-y plane, from x-axis towards y-axis.
					// pitch is theta. 0 = looking straight up, pi = looking straight down.

	Vec3d initialised_up;

	Vec3d target_pos; // Target point for trackball-style navigation

	Vec3d initial_rotation;

	double base_move_speed, base_rotate_speed;
	double move_speed_scale, mouse_sensitivity_scale;
	double lens_sensor_dist, lens_shift_up, lens_shift_right;

	// Spherical camera doesn't allow looking up or down. So for spherical camera, allow_pitching should be set to false.
	bool allow_pitching;
};

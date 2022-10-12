/*=====================================================================
CameraController.h
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <maths/vec3.h>
#include <maths/vec2.h>


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

	Vec3d getFirstPersonPosition() const;
	Vec3d getPosition() const; // Has third person offset if third person camera is enabled.
	void setPosition(const Vec3d& pos);

	void setMouseSensitivity(double sensitivity);
	void setMoveScale(double move_scale); // Adjust camera movement speed based on world scale

	void getBasis(Vec3d& right_out, Vec3d& up_out, Vec3d& forward_out) const;

	Vec4f vectorToCamSpace(const Vec4f& v) const;

	Vec3d getAngles() const; // Specified as (heading, pitch, roll).
	void resetRotation();
	void setAngles(const Vec3d& newangles);

	Vec3d getForwardsVec() const;
	Vec3d getRightVec() const;
	Vec3d getUpVec() const;

	static Vec3d getUpForForwards(const Vec3d& forwards, const Vec3d& singular_up);
	static void getBasisForAngles(const Vec3d& angles_in, const Vec3d& singular_up, Vec3d& right_out, Vec3d& up_out, Vec3d& forward_out);

	void setThirdPersonEnabled(bool enabled) { third_person = enabled; }
	bool thirdPersonEnabled() const { return third_person; }

	float getThirdPersonCamDist() const { return third_person_cam_dist; }

	void handleScrollWheelEvent(float delta_y);

	void setSelfieModeEnabled(double cur_time, bool enabled);
	bool selfieModeEnabled() const { return selfie_mode; }


	bool invert_mouse;
	bool invert_sideways_movement;

	static void test();

private:
	Vec3d position;
	Vec3d rotation; // Specified as (heading, pitch, roll).
					// heading is phi: rotation angle in x-y plane, from x-axis towards y-axis.
					// pitch is theta. 0 = looking straight up, pi = looking straight down.

	Vec3d initialised_up;

	Vec3d initial_rotation;

	double base_move_speed, base_rotate_speed;
	double move_speed_scale, mouse_sensitivity_scale;
	double lens_sensor_dist, lens_shift_up, lens_shift_right;

	bool third_person;

	bool selfie_mode;
	// For 3rd person/selfie cam:
	float start_cam_rot_z;
	float end_cam_rot_z;
	double start_transition_time;
	double end_transition_time;

	float third_person_cam_dist;
public:
	Vec3d third_person_cam_position;

	Vec3d current_third_person_target_pos; // Blended position
};

/*=====================================================================
CameraController.h
------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include <maths/Matrix4f.h>
#include <maths/vec3.h>
#include <maths/vec2.h>


/*=====================================================================
CameraController
----------------
=====================================================================*/
SSE_CLASS_ALIGN CameraController
{
public:
	//GLARE_ALIGNED_16_NEW_DELETE

	CameraController();
	~CameraController();

	void initialise(const Vec3d& cam_pos, const Vec3d& cam_forwards, const Vec3d& cam_up, double lens_sensor_dist, double lens_shift_up, double lens_shift_right);

	void updateRotation(double pitch_delta, double heading_delta);

	// For CameraMode_FixedAngle and CameraMode_TrackingCamera
	void setTargetObjectTransform(const Matrix4f& ob_to_world_matrix, const Matrix4f& y_forward_to_model_space_rot);

	Vec3d getFirstPersonPosition() const;
	Vec3d getPosition() const; // Has third person offset if third person camera is enabled.
	
	void setFirstPersonPosition(const Vec3d& pos); // Set just the first-person position, leave the third-person position unchanged.
	void setThirdPersonCamPosition(const Vec3d& pos);
	void setFirstAndThirdPersonPositions(const Vec3d& pos); // Set first person position to pos, Set a suitable third-person target and camera position set back from pos.

	void setMouseSensitivity(double sensitivity);
	void setMoveScale(double move_scale); // Adjust camera movement speed based on world scale

	void getWorldToCameraMatrix(Matrix4f& world_to_camera_matrix_out);

	Vec4f vectorToCamSpace(const Vec4f& v) const;

	
	Vec3d getAngles() const; // Specified as (heading, pitch, roll).
	Vec3d getAvatarAngles(); // For modes like CameraMode_FreeCamera, the camera angles change but the avatar angles remain as they were.

	void setAngles(const Vec3d& newangles);

	Vec3d getForwardsMoveVec() const; // The direction to move the avatar or the free camera.
	Vec3d getRightMoveVec() const;

	Vec3d getForwardsVec() const;
	Vec3d getRightVec() const;
	Vec3d getUpVec() const;

	static Vec3d getUpForForwards(const Vec3d& forwards, const Vec3d& singular_up);
	static void getBasisForAngles(const Vec3d& angles_in, const Vec3d& singular_up, Vec3d& right_out, Vec3d& up_out, Vec3d& forward_out);

	void setThirdPersonEnabled(bool enabled) { third_person = enabled; }
	bool thirdPersonEnabled() const { return third_person; }

	float getThirdPersonCamDist() const { return third_person_cam_dist; }

	// Returns true if we are in third person view and have zoomed in sufficiently far to change to first person view.
	bool handleScrollWheelEvent(float delta_y);

	void setSelfieModeEnabled(double cur_time, bool enabled);
	bool selfieModeEnabled() const { return current_cam_mode == CameraMode_Selfie; }

	void standardCameraModeSelected();
	void selfieCameraModeSelected();
	void fixedAngleCameraModeSelected();
	void freeCameraModeSelected();
	void trackingCameraModeSelected();

	void setFreeCamMovementDesiredVel(const Vec3f& vel);

	void think(double dt);


	enum CameraMode
	{
		CameraMode_Standard,
		CameraMode_Selfie,
		CameraMode_FixedAngle, // rotation defines a direction in target ob space, camera position displaced from target object backwards along that vector.
		CameraMode_FreeCamera, // Camera position is third_person_cam_position, cam rotation given by rotation.
		CameraMode_TrackingCamera // Camera position is third_person_cam_position, cam rotation given by vector to tracking target.
	};

	enum AutofocusMode
	{
		AutofocusMode_Off,
		AutofocusMode_Eye
	};

	void setAutofocusMode(AutofocusMode mode) { autofocus_mode = mode; }

	static void test();

private:
	static Vec3d getRotationAnglesFromCameraBasis(const Vec3d& camera_forward, const Vec3d& camera_right);
	void setStateBeforeCameraModeChange();
	Matrix4f getFixedAngleWorldToCamRotationMatrix() const;
	Vec3d fixedAngleCameraDir() const;
	Vec3d standardAnglesForFixedAngleCam() const;

	Matrix4f target_ob_to_world_matrix;
	Matrix4f y_forward_to_model_space_rot;

	Vec3d position;
	Vec3d rotation; // Specified as (heading, pitch, roll).
					// heading is phi: rotation angle in x-y plane, from x-axis towards y-axis.
					// pitch is theta. 0 = looking straight up, pi = looking straight down.
	Vec3d last_avatar_rotation; // With a non-standard camera mode that sets rotation, we want the avatar rotation to remain unchanged.

	Vec3d free_cam_desired_vel;
	Vec3d free_cam_vel; // Current free camera velocity

	Vec3d initialised_up;

	double base_move_speed, base_rotate_speed;
	double move_speed_scale, mouse_sensitivity_scale;

public:
	double lens_sensor_dist, lens_shift_up, lens_shift_right;
private:

	bool third_person;

	// For 3rd person/selfie cam:
	//float start_cam_rot_z;
	//float end_cam_rot_z;
	//double start_transition_time;
	//double end_transition_time;

public:
	CameraMode current_cam_mode;
private:

	float third_person_cam_dist;
	
	Vec3d third_person_cam_position;
public:
	Vec3d current_third_person_target_pos; // Blended position

	bool invert_mouse;
	bool invert_sideways_movement;

	AutofocusMode autofocus_mode;
};

/*=====================================================================
BikePhysics.cpp
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "BikePhysics.h"


#include "PhysicsWorld.h"
#include "PhysicsObject.h"
#include "JoltUtils.h"
#include <StringUtils.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>


BikePhysics::BikePhysics(JPH::BodyID car_body_id_, BikePhysicsSettings settings_, PhysicsWorld& physics_world)
{
	m_physics_world = &physics_world;

	cur_steering_right = 0;
	smoothed_desired_roll_angle = 0;

	car_body_id = car_body_id_;
	settings = settings_;
	unflip_up_force_time_remaining = -1;
	cur_seat_index = 0;

	const float wheel_radius = 0.353f; // 0.3f;
	const float wheel_width = 0.1f;
	const float half_vehicle_length = 1.7f;
	const float half_vehicle_height = 0.3f;
	const float suspension_min_length = 0.2f;
	const float suspension_max_length = 0.4f;
	const float max_steering_angle = JPH::DegreesToRadians(60);


	collision_tester = new JPH::VehicleCollisionTesterCastSphere(Layers::MOVING, 0.5f * wheel_width, /*inUp=*/JPH::Vec3(0,0,1));

	JPH::BodyLockWrite lock(physics_world.physics_system->GetBodyLockInterface(), car_body_id);
	if(lock.Succeeded())
	{
		JPH::Body& bike_body = lock.GetBody();
		
		// Create vehicle constraint
		JPH::VehicleConstraintSettings vehicle;
		vehicle.mUp = JPH::Vec3(0,0,1);
		vehicle.mForward = JPH::Vec3(0,1,0);
		vehicle.mMaxPitchRollAngle = JPH::DegreesToRadians(5.0f);
		//vehicle.mMaxPitchAngle = JPH::DegreesToRadians(85.0f);
		//vehicle.mMaxRollAngle = JPH::DegreesToRadians(1.f);


		// Wheels

		// Front wheel
		const float handbrake_torque = 10000; // default is 4000.
		JPH::WheelSettingsWV* front_wheel = new JPH::WheelSettingsWV;
		front_wheel->mPosition = JPH::Vec3(0, half_vehicle_length - 2.0f * wheel_radius, -1.0f * half_vehicle_height);
		front_wheel->mMaxSteerAngle = max_steering_angle;
		front_wheel->mMaxHandBrakeTorque = handbrake_torque * 0.02f; // Front wheel doesn't have hand brake
		front_wheel->mDirection = JPH::Vec3(0,-0.5,-1).Normalized(); // Direction of the suspension in local space of the body
		front_wheel->mRadius = wheel_radius;
		front_wheel->mWidth = wheel_width;
		front_wheel->mSuspensionMinLength = suspension_min_length;
		front_wheel->mSuspensionMaxLength = suspension_max_length;

		// Rear wheel
		JPH::WheelSettingsWV* rear_wheel = new JPH::WheelSettingsWV;
		rear_wheel->mPosition = JPH::Vec3(0, -half_vehicle_length + 2.0f * wheel_radius, -1.0f * half_vehicle_height);
		rear_wheel->mMaxSteerAngle = 0.f;
		rear_wheel->mMaxHandBrakeTorque = handbrake_torque;
		rear_wheel->mDirection = JPH::Vec3(0,0,-1).Normalized(); // Direction of the suspension in local space of the body
		rear_wheel->mRadius = wheel_radius;
		rear_wheel->mWidth = wheel_width;
		rear_wheel->mSuspensionMinLength = suspension_min_length;
		rear_wheel->mSuspensionMaxLength = suspension_max_length;

		vehicle.mWheels = { front_wheel, rear_wheel };

		for (JPH::WheelSettings *w : vehicle.mWheels)
		{
			dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[0].mY *= 5.;
			dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[1].mY *= 5.;
			dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[2].mY *= 5.;
		//	
		//	dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[0].mY *= 10.;
		//	dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[1].mY *= 10.;
		//	dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[2].mY *= 10.;
		}


		JPH::WheeledVehicleControllerSettings* controller_settings = new JPH::WheeledVehicleControllerSettings;
		vehicle.mController = controller_settings;

		// Differential

		// Front wheel drive:
		controller_settings->mDifferentials.resize(1);
		controller_settings->mDifferentials[0].mLeftWheel = 1; // set to rear wheel index
		controller_settings->mDifferentials[0].mRightWheel = -1; // no right wheel.
		controller_settings->mDifferentials[0].mLeftRightSplit = 0; // Apply all torque to 'left' (front) wheel.

	
		controller_settings->mEngine.mMaxTorque = 50;
		controller_settings->mEngine.mMaxRPM = 10000; // If only 1 gear, allow a higher max RPM
		controller_settings->mEngine.mInertia = 0.05; // If only 1 gear, allow a higher max RPM

		//controller_settings->mTransmission.mMode = JPH::ETransmissionMode::Manual;
		controller_settings->mTransmission.mGearRatios = JPH::Array<float>(1, 2.66f); // Use a single forwards gear


		vehicle_constraint = new JPH::VehicleConstraint(bike_body, vehicle);
		physics_world.physics_system->AddConstraint(vehicle_constraint);
		physics_world.physics_system->AddStepListener(vehicle_constraint);

		// Set the collision tester
		vehicle_constraint->SetVehicleCollisionTester(collision_tester);
	}
}


BikePhysics::~BikePhysics()
{
	m_physics_world->physics_system->RemoveConstraint(vehicle_constraint);
	m_physics_world->physics_system->RemoveStepListener(vehicle_constraint);
	vehicle_constraint = NULL;
	collision_tester = NULL;
}


VehiclePhysicsUpdateEvents BikePhysics::update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime)
{
	VehiclePhysicsUpdateEvents events;

	if(cur_seat_index == 0)
	{
		float forward = 0.0f, right = 0.0f, up_input = 0.f, brake = 0.0f, hand_brake = 0.0f;
		// Determine acceleration and brake
		if (physics_input.W_down || physics_input.up_down)
			forward = 1.0f;
		else if(physics_input.S_down || physics_input.down_down)
			forward = -1.0f;

		if(physics_input.SHIFT_down) // boost!
			forward *= 2.f;


		// Hand brake will cancel gas pedal
		if(physics_input.space_down)
		{
			hand_brake = 1.0f;
			up_input = 1.f;
		}

		if(physics_input.C_down || physics_input.CTRL_down)
			up_input = -1.f;

		// Steering
		const float STEERING_SPEED = 2.f;
		if(physics_input.A_down && !physics_input.D_down)
			cur_steering_right = myClamp(cur_steering_right - STEERING_SPEED * dtime, -1.f, 1.f);
		else if(physics_input.D_down && !physics_input.A_down)
			cur_steering_right = myClamp(cur_steering_right + STEERING_SPEED * dtime, -1.f, 1.f);
		else
		{
			if(cur_steering_right > 0)
				cur_steering_right = myMax(cur_steering_right - STEERING_SPEED * dtime, 0.f); // Relax to neutral steering position
			else if(cur_steering_right < 0)
				cur_steering_right = myMin(cur_steering_right + STEERING_SPEED * dtime, 0.f); // Relax to neutral steering position
		}

		right = cur_steering_right;
	

		JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();

		// On user input, assure that the car is active
		if (right != 0.0f || forward != 0.0f || brake != 0.0f || hand_brake != 0.0f)
			body_interface.ActivateBody(car_body_id);


		// Pass the input on to the constraint
		JPH::WheeledVehicleController* controller = static_cast<JPH::WheeledVehicleController*>(vehicle_constraint->GetController());
		controller->SetDriverInput(forward, right, brake, hand_brake);


		const JPH::Mat44 transform = body_interface.GetWorldTransform(car_body_id);

		JPH::Float4 cols[4];
		transform.StoreFloat4x4(cols);

		const Matrix4f to_world(&cols[0].x);

		// Vectors in y-forward space
		const Vec4f forwards_y_for(0,1,0,0);
		const Vec4f right_y_for(1,0,0,0);
		const Vec4f up_y_for(0,0,1,0);

		// model/object space to y-forward space = R
		// y-forward space to model/object space = R^-1

		// The particular R will depend on the space the modeller chose.

		const JPH::Quat R_quat = toJoltQuat(settings.script_settings.model_to_y_forwards_rot_2 * settings.script_settings.model_to_y_forwards_rot_1);

		const Matrix4f R_inv = ((settings.script_settings.model_to_y_forwards_rot_2 * settings.script_settings.model_to_y_forwards_rot_1).conjugate()).toMatrix();

		//TEMP make bike float for testing constraints:
		const Vec4f up_force = Vec4f(0,0,1,0) * settings.bike_mass * 9.81;
		//body_interface.AddForce(car_body_id, toJoltVec3(up_force));


		const Vec4f forwards_os = R_inv * forwards_y_for;
		const Vec4f right_os = R_inv * right_y_for;
		const Vec4f up_os = crossProduct(right_os, forwards_os);

		const Vec4f right_vec_ws = to_world * right_os;
		const Vec4f forward_vec_ws = to_world * forwards_os;
		const Vec4f up_vec_ws = to_world * up_os;

		// If wheels are not touching anything, allow pitch control
		if(!vehicle_constraint->GetWheel(0)->HasContact() && !vehicle_constraint->GetWheel(1)->HasContact())
		{
			const Vec4f pitch_control_torque = right_vec_ws * settings.bike_mass * 1.f * up_input;
			body_interface.AddTorque(car_body_id, toJoltVec3(pitch_control_torque));
		}


		const float total_lateral_lambda = vehicle_constraint->GetWheel(0)->GetLateralLambda() + vehicle_constraint->GetWheel(1)->GetLateralLambda();

		const float use_dt = dtime;
		const float use_lateral_force = total_lateral_lambda / use_dt;

		const float N_mag = 9.81f * settings.bike_mass; // Magnitude of normal force upwards from ground
		const float f_f_mag = use_lateral_force;

		const float ratio = f_f_mag / N_mag;
		const float desired_roll_angle = std::atan(ratio);

		const Vec4f no_roll_right_ws = crossProduct(forward_vec_ws, Vec4f(0,0,1,0));
		const Vec4f no_roll_up_ws = crossProduct(no_roll_right_ws, forward_vec_ws);

		const float cur_roll_angle = atan2(dot(up_vec_ws, no_roll_right_ws), dot(up_vec_ws, no_roll_up_ws));
		
		// Get axis we want to rotate around
		const float roll_angle = desired_roll_angle - cur_roll_angle;

		const float lerp_factor = myMin(0.1f, dtime * 10.f);
		smoothed_desired_roll_angle = smoothed_desired_roll_angle * (1 - lerp_factor) + lerp_factor * desired_roll_angle;

		// Choose a desired angular velocity which is proportional in magnitude to how far we need to rotate.
		// Note that we can't just apply a constant torque in the desired rotation direction, or we get angular oscillations around the desired rotation.
		const JPH::Vec3 desired_angular_vel = toJoltVec3((forward_vec_ws * roll_angle) * 3);

		// Apply a torque to try and match the desired angular velocity.
		const JPH::Vec3 angular_vel = body_interface.GetAngularVelocity(car_body_id);
		const JPH::Vec3 correction_torque = (desired_angular_vel - angular_vel) * settings.bike_mass * 1.5f;
		//body_interface.AddTorque(car_body_id, correction_torque);

		//body_interface.AddForce(car_body_id, toJoltVec3(right_vec_ws * roll_angle * 500.f), body_interface.GetCenterOfMassPosition(car_body_id) + toJoltVec3(up_vec_ws) * 1.f);

		//vehicle_constraint->SetTiltAngle(smoothed_desired_roll_angle);

		// conPrint("RPM: " + doubleToStringNDecimalPlaces(controller->GetEngine().GetCurrentRPM(), 1));
		// conPrint("engine torque: " + doubleToStringNDecimalPlaces(controller->GetEngine().GetTorque(forward), 1));
		// conPrint("current gear: " + toString(controller->GetTransmission().GetCurrentGear()));

	} // end if seat == 0
	
	// const float speed_km_h = v_mag * (3600.0f / 1000.f);
	// conPrint("speed (km/h): " + doubleToStringNDecimalPlaces(speed_km_h, 1));

	return events;
}


Vec4f BikePhysics::getFirstPersonCamPos(PhysicsWorld& physics_world) const
{
	const Matrix4f seat_to_world = getSeatToWorldTransform(physics_world);
	return seat_to_world * Vec4f(0,0,0.6f,1); // Raise camera position to appox head position
}


Matrix4f BikePhysics::getBodyTransform(PhysicsWorld& physics_world) const
{
	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();
	const JPH::Mat44 transform = body_interface.GetWorldTransform(car_body_id);

	JPH::Float4 cols[4];
	transform.StoreFloat4x4(cols);

	return Matrix4f(&cols[0].x);
}


Matrix4f BikePhysics::getWheelToWorldTransform(PhysicsWorld& physics_world, int wheel_index) const
{
	const JPH::Mat44 wheel_transform = vehicle_constraint->GetWheelWorldTransform(wheel_index, JPH::Vec3::sAxisY(), JPH::Vec3::sAxisX()); // The cyclinder we draw is aligned with Y so we specify that as rotational axis

	JPH::Float4 cols[4];
	wheel_transform.StoreFloat4x4(cols);

	return Matrix4f(&cols[0].x);
}


// Sitting position is (0,0,0) in seat space, forwards is (0,1,0), right is (1,0,0)

// Seat to world = object to world * seat to object
// model/object space to y-forward space = R
// y-forward space to model/object space = R^-1
// 
// seat_to_object = seat_translation_model_space * R^1
//
// So  
// Seat_to_world = object_to_world * seat_translation_model_space * R^1
Matrix4f BikePhysics::getSeatToWorldTransform(PhysicsWorld& physics_world) const
{ 
	const Matrix4f R_inv = ((settings.script_settings.model_to_y_forwards_rot_2 * settings.script_settings.model_to_y_forwards_rot_1).conjugate()).toMatrix();

	// Seat to world = object to world * seat to object
	return getBodyTransform(physics_world) * Matrix4f::translationMatrix(settings.script_settings.seat_settings[this->cur_seat_index].seat_position) * R_inv;
}


Vec4f BikePhysics::getLinearVel(PhysicsWorld& physics_world) const
{
	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();
	return toVec4fVec(body_interface.GetLinearVelocity(car_body_id));
}

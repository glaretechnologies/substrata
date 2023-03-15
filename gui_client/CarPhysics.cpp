/*=====================================================================
CarPhysics.cpp
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "CarPhysics.h"


#include "CameraController.h"
#include "PhysicsWorld.h"
#include "PhysicsObject.h"
#include "JoltUtils.h"
#include <StringUtils.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>


CarPhysics::CarPhysics()
{
	cur_steering_right = 0;
}


CarPhysics::~CarPhysics()
{
}


void CarPhysics::init(PhysicsWorld& physics_world)
{
	const float wheel_radius = 0.353f; // 0.3f;
	const float wheel_width = 0.1f;
	const float half_vehicle_length = 2.02f;
	const float half_vehicle_width = 0.83f;//0.9f;
	const float half_vehicle_height = 0.2f;
	const float suspension_min_length = 0.3f;
	const float suspension_max_length = 0.4f;
	const float max_steering_angle = JPH::DegreesToRadians(30);

	// Create collision testers
	//mTesters[0] = new JPH::VehicleCollisionTesterRay(Layers::MOVING);
	//mTesters[1] = new JPH::VehicleCollisionTesterCastSphere(Layers::MOVING, 0.5f * wheel_width);

	//mTester = new JPH::VehicleCollisionTesterCastSphere(Layers::NON_MOVING/*MOVING*/, 0.5f * wheel_width, /*inUp=*/JPH::Vec3(0,0,1));
	mTester = new JPH::VehicleCollisionTesterCastSphere(Layers::MOVING, 0.5f * wheel_width, /*inUp=*/JPH::Vec3(0,0,1));

	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();

	// Create vehicle body
	JPH::Vec3 position(0, 0, 2);
	JPH::RefConst<JPH::Shape> car_shape = JPH::OffsetCenterOfMassShapeSettings(JPH::Vec3(0, 0, -half_vehicle_height), new JPH::BoxShape(JPH::Vec3(half_vehicle_width, half_vehicle_length, half_vehicle_height))).Create().Get();
	//JPH::RefConst<JPH::Shape> car_shape = JPH::OffsetCenterOfMassShapeSettings(JPH::Vec3(0, -half_vehicle_height, 0), new JPH::BoxShape(JPH::Vec3(half_vehicle_width, half_vehicle_height, half_vehicle_length))).Create().Get();
	JPH::BodyCreationSettings car_body_settings(car_shape, position, JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Layers::MOVING);
	car_body_settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
	car_body_settings.mMassPropertiesOverride.mMass = 1500.0f;
	jolt_body = body_interface.CreateBody(car_body_settings);
	body_interface.AddBody(jolt_body->GetID(), JPH::EActivation::Activate);

	// Create vehicle constraint
	JPH::VehicleConstraintSettings vehicle;
	vehicle.mUp = JPH::Vec3(0,0,1);
	vehicle.mForward = JPH::Vec3(0,1,0);
	//vehicle.mDrawConstraintSize = 0.1f;
	vehicle.mMaxPitchAngle = JPH::DegreesToRadians(60.0f);
	vehicle.mMaxRollAngle = JPH::DegreesToRadians(60.f);
//	vehicle.mMaxPitchRollAngle = JPH::DegreesToRadians(5.0f);

	// Wheels

	const float handbrake_torque = 10000; // default is 4000.
	JPH::WheelSettingsWV *w1 = new JPH::WheelSettingsWV;
	w1->mPosition = JPH::Vec3(-half_vehicle_width, half_vehicle_length - 2.0f * wheel_radius, -0.9f * half_vehicle_height);
	w1->mMaxSteerAngle = max_steering_angle;
	w1->mMaxHandBrakeTorque = 0.0f; // Front wheel doesn't have hand brake

	JPH::WheelSettingsWV *w2 = new JPH::WheelSettingsWV;
	w2->mPosition = JPH::Vec3(half_vehicle_width, half_vehicle_length - 2.0f * wheel_radius, -0.9f * half_vehicle_height);
	w2->mMaxSteerAngle = max_steering_angle;
	w2->mMaxHandBrakeTorque = 0.0f; // Front wheel doesn't have hand brake

	JPH::WheelSettingsWV *w3 = new JPH::WheelSettingsWV;
	w3->mPosition = JPH::Vec3(-half_vehicle_width, -half_vehicle_length + 2.0f * wheel_radius, -0.9f * half_vehicle_height);
	w3->mMaxSteerAngle = 0.0f;
	w3->mMaxHandBrakeTorque = handbrake_torque;

	JPH::WheelSettingsWV *w4 = new JPH::WheelSettingsWV;
	w4->mPosition = JPH::Vec3(half_vehicle_width, -half_vehicle_length + 2.0f * wheel_radius, -0.9f * half_vehicle_height);
	w4->mMaxSteerAngle = 0.0f;
	w4->mMaxHandBrakeTorque = handbrake_torque;

	vehicle.mWheels = { w1, w2, w3, w4 };

	for (JPH::WheelSettings *w : vehicle.mWheels)
	{
		w->mRadius = wheel_radius;
		w->mWidth = wheel_width;
		w->mSuspensionMinLength = suspension_min_length;
		w->mSuspensionMaxLength = suspension_max_length;

		w->mDirection = JPH::Vec3(0,0,-1); // Direction of the suspension in local space of the body

		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[0].mY *= 2.;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[1].mY *= 2.;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[2].mY *= 2.;

		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[0].mY *= 1.;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[1].mY *= 1.;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[2].mY *= 1.;
	}

	JPH::WheeledVehicleControllerSettings *controller = new JPH::WheeledVehicleControllerSettings;
	vehicle.mController = controller;

	// Differential

	// Front wheel drive:
	controller->mDifferentials.resize(1);
	controller->mDifferentials[0].mLeftWheel = 0;
	controller->mDifferentials[0].mRightWheel = 1;

	// 4WD:
//	controller->mDifferentials.resize(2);
//	controller->mDifferentials[0].mLeftWheel = 0;// front
//	controller->mDifferentials[0].mRightWheel = 1;
//	controller->mDifferentials[0].mEngineTorqueRatio = 0.5f;
//	controller->mDifferentials[1].mLeftWheel = 2; // back
//	controller->mDifferentials[1].mRightWheel = 3;
//	controller->mDifferentials[1].mEngineTorqueRatio = 0.5f;

	// Rear wheel drive:
	/*controller->mDifferentials.resize(1);
	controller->mDifferentials[0].mLeftWheel = 2;
	controller->mDifferentials[0].mRightWheel = 3;*/

	controller->mEngine.mMaxTorque = 1000;
	controller->mEngine.mMaxRPM = 20000;

	//controller->mTransmission.mMode = JPH::ETransmissionMode::Manual;
	//controller->mTransmission.mGearRatios = JPH::Array<float>(1, 2.9f); // Use a single forwards gear

	// Anti rollbars
	vehicle.mAntiRollBars.resize(2);
	vehicle.mAntiRollBars[0].mLeftWheel = 0;
	vehicle.mAntiRollBars[0].mRightWheel = 1;
	vehicle.mAntiRollBars[1].mLeftWheel = 2;
	vehicle.mAntiRollBars[1].mRightWheel = 3;

	mVehicleConstraint = new JPH::VehicleConstraint(*jolt_body, vehicle);
	physics_world.physics_system->AddConstraint(mVehicleConstraint);
	physics_world.physics_system->AddStepListener(mVehicleConstraint);


	// Set the collision tester
	mVehicleConstraint->SetVehicleCollisionTester(mTester);
}


void CarPhysics::shutdown()
{
	//jolt_character->RemoveFromPhysicsSystem();
	jolt_body = NULL;
}


CarPhysicsUpdateEvents CarPhysics::update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float raw_dtime, Vec4f& campos_in_out)
{
	CarPhysicsUpdateEvents events;
	
	// Determine acceleration and brake
	float forward = 0.0f, right = 0.0f, brake = 0.0f, hand_brake = 0.0f;
	if (physics_input.W_down || physics_input.up_down)
		forward = 1.0f;
	else if(physics_input.S_down || physics_input.down_down)
		forward = -1.0f;

	// Check if we're reversing direction
	//if (mPreviousForward * forward < 0.0f)
	//{
	//	// Get vehicle velocity in local space to the body of the vehicle
	//	float velocity = (mCarBody->GetRotation().Conjugated() * mCarBody->GetLinearVelocity()).GetZ();
	//	if ((forward > 0.0f && velocity < -0.1f) || (forward < 0.0f && velocity > 0.1f))
	//	{
	//		// Brake while we've not stopped yet
	//		forward = 0.0f;
	//		brake = 1.0f;
	//	}
	//	else
	//	{
	//		// When we've come to a stop, accept the new direction
	//		mPreviousForward = forward;
	//	}
	//}

	// Hand brake will cancel gas pedal
	if(physics_input.space_down)
	{
		//forward = 0.0f;
		hand_brake = 1.0f;
	}

	// Steering
	/*if(physics_input.A_down)
		right = -1.0f;
	else if(physics_input.D_down)
		right = 1.0f;*/

	const float STEERING_SPEED = 3.f;
	if(physics_input.A_down && !physics_input.D_down)
		cur_steering_right = myClamp(cur_steering_right - STEERING_SPEED * (float)raw_dtime, -1.f, 1.f);
	else if(physics_input.D_down && !physics_input.A_down)
		cur_steering_right = myClamp(cur_steering_right + STEERING_SPEED * (float)raw_dtime, -1.f, 1.f);
	else
	{
		if(cur_steering_right > 0)
			cur_steering_right = myMax(cur_steering_right - STEERING_SPEED * (float)raw_dtime, 0.f); // Relax to neutral steering position
		else if(cur_steering_right < 0)
			cur_steering_right = myMin(cur_steering_right + STEERING_SPEED * (float)raw_dtime, 0.f); // Relax to neutral steering position
	}

	right = cur_steering_right;

	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();

	// On user input, assure that the car is active
	if (right != 0.0f || forward != 0.0f || brake != 0.0f || hand_brake != 0.0f)
		body_interface.ActivateBody(jolt_body->GetID());

	// Pass the input on to the constraint
	JPH::WheeledVehicleController* controller = static_cast<JPH::WheeledVehicleController *>(mVehicleConstraint->GetController());
	controller->SetDriverInput(forward, right, brake, hand_brake);


	//conPrint("car pos: " + toVec3f(mVehicleConstraint->GetVehicleBody()->GetPosition()).toString());

	conPrint("RPM: " + doubleToStringNDecimalPlaces(controller->GetEngine().GetCurrentRPM(), 1));
	conPrint("engine torque: " + doubleToStringNDecimalPlaces(controller->GetEngine().GetTorque(forward), 1));
	conPrint("current gear: " + toString(controller->GetTransmission().GetCurrentGear()));

	const float speed_km_h = mVehicleConstraint->GetVehicleBody()->GetLinearVelocity().Length() * (3600.0f / 1000.f);
	conPrint("speed (km/h): " + doubleToStringNDecimalPlaces(speed_km_h, 1));


	campos_in_out = toVec3f(mVehicleConstraint->GetVehicleBody()->GetPosition()).toVec4fPoint() + Vec4f(0,0,2,0);

	return events;
}


Matrix4f CarPhysics::getWheelTransform(int i)
{
#if USE_JOLT
	const JPH::Mat44 wheel_transform = mVehicleConstraint->GetWheelWorldTransform(i, JPH::Vec3::sAxisY(), JPH::Vec3::sAxisX()); // The cyclinder we draw is aligned with Y so we specify that as rotational axis

	JPH::Float4 cols[4];
	wheel_transform.StoreFloat4x4(cols);

	return Matrix4f(&cols[0].x);
#else
	return Matrix4f::identity();
#endif
}


Matrix4f CarPhysics::getBodyTransform()
{
#if USE_JOLT
	const JPH::Mat44 transform = mVehicleConstraint->GetVehicleBody()->GetCenterOfMassTransform();

	JPH::Float4 cols[4];
	transform.StoreFloat4x4(cols);

	return Matrix4f(&cols[0].x);
#else
	return Matrix4f::identity();
#endif
}

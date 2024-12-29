/*=====================================================================
BikePhysics.cpp
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "BikePhysics.h"


#include "PhysicsWorld.h"
#include "PhysicsObject.h"
#include "ParticleManager.h"
#include "JoltUtils.h"
#include <opengl/OpenGLEngine.h>
#include <StringUtils.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include "../audio/AudioFileReader.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Vehicle/MotorcycleController.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>


static const float ob_to_world_scale = 0.18f; // For particular concept bike model exported from Blender
static const float world_to_ob_scale = 1 / ob_to_world_scale;


static const float wheel_radius =  3.856f / 2 * ob_to_world_scale; // = 0.34704 m
static const float wheel_width = 0.94f * ob_to_world_scale;
static const float half_vehicle_width = 1.7f/*2.f*/ / 2 * ob_to_world_scale;
static const float half_vehicle_length = 9.f/*10.f*/ / 2 * ob_to_world_scale;
static const float half_vehicle_height = 3.2f/*3.5f*/ / 2 * ob_to_world_scale;
static const float max_steering_angle = JPH::DegreesToRadians(30);

static const float lean_spring_constant = 2000.f;
static const float lean_spring_damping = 500.f; // This seems to cause the instability


BikePhysics::BikePhysics(WorldObjectRef object, BikePhysicsSettings settings_, PhysicsWorld& physics_world, 
	glare::AudioEngine* audio_engine, const std::string& base_dir_path, ParticleManager* particle_manager_)
:	m_opengl_engine(NULL),
	particle_manager(particle_manager_)
	//last_roll_error(0)
{
	world_object = object.ptr();
	m_physics_world = &physics_world;
	m_audio_engine = audio_engine;

	user_in_driver_seat = false;

	cur_steering_right = 0;
	smoothed_desired_roll_angle = 0;
	cur_target_tilt_angle = 0;

	settings = settings_;
	righting_time_remaining = -1;
	last_desired_up_vec = Vec4f(0,0,0,0);
	last_force_vec = Vec4f(0,0,0,0);


	const Matrix4f z_up_to_model_space = ((settings.script_settings->model_to_y_forwards_rot_2 * settings.script_settings->model_to_y_forwards_rot_1).conjugate()).toMatrix();

	const Vec4f cur_pos = object->physics_object->pos;
	const Quatf cur_rot = object->physics_object->rot;


	// Remove existing bike physics object
	physics_world.removeObject(object->physics_object);

	const Vec4f box_half_extents_ms = abs(z_up_to_model_space * Vec4f(half_vehicle_width, half_vehicle_length, half_vehicle_height, 0));

	// Instead of a box, use a convex hull, with some extra points on the sides, and one on the top.
	// The problem with a box is that it gets stuck lying with a side or the top on the ground.
	// With the right convex hull the bike will right itself.
	convex_hull_pts.push_back(JPH::Vec3( box_half_extents_ms[0],  box_half_extents_ms[1],  box_half_extents_ms[2]));
	convex_hull_pts.push_back(JPH::Vec3( box_half_extents_ms[0],  box_half_extents_ms[1], -box_half_extents_ms[2]));
	convex_hull_pts.push_back(JPH::Vec3( box_half_extents_ms[0], -box_half_extents_ms[1],  box_half_extents_ms[2]));
	convex_hull_pts.push_back(JPH::Vec3( box_half_extents_ms[0], -box_half_extents_ms[1], -box_half_extents_ms[2]));
	convex_hull_pts.push_back(JPH::Vec3(-box_half_extents_ms[0],  box_half_extents_ms[1],  box_half_extents_ms[2]));
	convex_hull_pts.push_back(JPH::Vec3(-box_half_extents_ms[0],  box_half_extents_ms[1], -box_half_extents_ms[2]));
	convex_hull_pts.push_back(JPH::Vec3(-box_half_extents_ms[0], -box_half_extents_ms[1],  box_half_extents_ms[2]));
	convex_hull_pts.push_back(JPH::Vec3(-box_half_extents_ms[0], -box_half_extents_ms[1], -box_half_extents_ms[2]));

	// Add side points
	convex_hull_pts.push_back(toJoltVec3(z_up_to_model_space * Vec4f(-half_vehicle_width - 0.1f, 0, 0.1f, 0)));
	convex_hull_pts.push_back(toJoltVec3(z_up_to_model_space * Vec4f( half_vehicle_width + 0.1f, 0, 0.1f, 0)));

	// Add top point
	convex_hull_pts.push_back(toJoltVec3(z_up_to_model_space * Vec4f(0, 0, half_vehicle_height + 0.05f, 0)));

	JPH::Ref<JPH::ConvexHullShapeSettings> hull_shape_settings = new JPH::ConvexHullShapeSettings(
		convex_hull_pts
	);
	JPH::Ref<JPH::Shape> convex_hull_shape = hull_shape_settings->Create().Get();

	JPH::Ref<JPH::Shape> bike_body_shape = JPH::OffsetCenterOfMassShapeSettings(toJoltVec3(z_up_to_model_space * Vec4f(0,0,-0.15f,0)), // TEMP: hard-coded centre of mass offset.
		convex_hull_shape
	).Create().Get();


	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();

	// Create vehicle body
	JPH::BodyCreationSettings bike_body_settings(bike_body_shape, toJoltVec3(cur_pos), toJoltQuat(cur_rot), JPH::EMotionType::Dynamic, Layers::VEHICLES);
	bike_body_settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
	bike_body_settings.mMassPropertiesOverride.mMass = settings_.bike_mass;
	bike_body_settings.mUserData = (uint64)object->physics_object.ptr();
	JPH::Body* bike_body = body_interface.CreateBody(bike_body_settings);

	this->bike_body_id                   = bike_body->GetID();
	object->physics_object->jolt_body_id = bike_body->GetID();
	
	body_interface.AddBody(bike_body->GetID(), JPH::EActivation::DontActivate);

	physics_world.addObject(object->physics_object);

		
	// Create vehicle constraint
	JPH::VehicleConstraintSettings vehicle;
	vehicle.mUp			= toJoltVec3(z_up_to_model_space * Vec4f(0,0,1,0));
	//vehicle.mWorldUp	= toJoltVec3(Vec4f(0,0,1,0));
	vehicle.mForward	= toJoltVec3(z_up_to_model_space * Vec4f(0,1,0,0));
	//vehicle.mMaxRollAngle = JPH::DegreesToRadians(1.f);


	// Wheels

	const Vec4f steering_axis_z_up = normalise(Vec4f(0, -1.87f, 2.37f, 0)); // = front suspension dir

	// Front wheel
	JPH::WheelSettingsWV* front_wheel = new JPH::WheelSettingsWV;
	front_wheel->mPosition				= toJoltVec3(z_up_to_model_space * Vec4f(0, 0.65f, 0.0f, 0)); // suspension attachment point
	//front_wheel->mDirection			=  toJoltVec3(z_up_to_model_space * -steering_axis_z_up); // Direction of the suspension in local space of the body, should point down
	front_wheel->mSuspensionDirection	= toJoltVec3(z_up_to_model_space * -steering_axis_z_up); // Direction of the suspension in local space of the body
	front_wheel->mSteeringAxis			= toJoltVec3(z_up_to_model_space *  steering_axis_z_up);
	front_wheel->mWheelUp				= toJoltVec3(z_up_to_model_space *  steering_axis_z_up);
	front_wheel->mWheelForward			= toJoltVec3(z_up_to_model_space * Vec4f(0,1,0,0));
	front_wheel->mSuspensionMinLength	= 0.1f;/*0.25f*/;
	front_wheel->mSuspensionMaxLength	= 0.35f;
	front_wheel->mSuspensionSpring.mFrequency = 2.0f; // Make the suspension a bit stiffer than default
	front_wheel->mRadius				= wheel_radius;
	front_wheel->mWidth					= wheel_width;
	front_wheel->mMaxSteerAngle			= max_steering_angle;
	front_wheel->mMaxHandBrakeTorque	= 40000.f; // handbrake = Front brake
	front_wheel->mMaxBrakeTorque		= 500.f;
	// Typical front wheel weight without tire is 6kg (https://www.amazon.com/Arashi-Z900-Z650-Ninja-650/dp/B0952XBB4V), front tire is 8.8 lb (4kg) (https://www.amazon.com/Pirelli-Diablo-Rosso-70ZR-17-D-Spec/dp/B06X6J4LXM)
	// Using solid cylinder moment of inertia for front wheel: 1/2 * 6 kg * 0.3 m^2 = 0.27 kg m^2
	// Front tire: Using hoop moment of inertia gives M * R^2 = 4 * 0.3^2 = 0.36 kg m^2
	front_wheel->mInertia				= 0.63f; // Total = 0.27 kg m^2 + 0.36 kg m^2

	

	// Rear wheel
	JPH::WheelSettingsWV* rear_wheel = new JPH::WheelSettingsWV;
	rear_wheel->mPosition = toJoltVec3(z_up_to_model_space * Vec4f(0, -0.88f, 0.0f, 0));
	//rear_wheel->mDirection			= toJoltVec3(z_up_to_model_space * Vec4f(0,0,-1,0)); // Direction of the suspension in local space of the body, should point down
	rear_wheel->mSuspensionDirection	= toJoltVec3(z_up_to_model_space * Vec4f(0,0,-1,0)); // Direction of the suspension in local space of the body
	rear_wheel->mSteeringAxis			= toJoltVec3(z_up_to_model_space * Vec4f(0,0,1,0));
	rear_wheel->mWheelUp				= toJoltVec3(z_up_to_model_space * Vec4f(0,0,1,0));
	rear_wheel->mWheelForward			= toJoltVec3(z_up_to_model_space * Vec4f(0,1,0,0));
	rear_wheel->mSuspensionMinLength	= 0.1f;
	rear_wheel->mSuspensionMaxLength	= 0.3f;
	rear_wheel->mSuspensionSpring.mFrequency = 2.5f; // Make the suspension a bit stiffer than default
	// rear tire is e.g. 13lb, see https://www.amazon.com/Pirelli-Diablo-Rosso-Rear-55ZR-17/dp/B01AZ5T50K?th=1
	rear_wheel->mRadius					= wheel_radius;
	rear_wheel->mWidth					= wheel_width;
	rear_wheel->mMaxSteerAngle			= 0.f;
	rear_wheel->mMaxHandBrakeTorque		= 0;
	rear_wheel->mMaxBrakeTorque			= 700.f;


	vehicle.mWheels = { front_wheel, rear_wheel };

	for (JPH::WheelSettings *w : vehicle.mWheels)
	{
		//dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[0].mY *= 15.; // 5.;
		//dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[1].mY *= 15.; // 5.;
		//dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[2].mY *= 15.; // 5.; 
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[0].mY = 15;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[1].mY = 8;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[2].mY = 3;

		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[0].mY *= 5.;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[1].mY *= 3.;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[2].mY *= 2.;
	}


	//JPH::WheeledVehicleControllerSettings* controller_settings = new JPH::WheeledVehicleControllerSettings;
	JPH::MotorcycleControllerSettings* controller_settings = new JPH::MotorcycleControllerSettings();
	vehicle.mController = controller_settings;

	controller_settings->mLeanSpringConstant = lean_spring_constant;
	controller_settings->mLeanSpringIntegrationCoefficient = 2000.f;
	controller_settings->mLeanSpringDamping = lean_spring_damping;
	controller_settings->mLeanSmoothingFactor = 0.9f;
	controller_settings->mMaxLeanAngle = JPH::DegreesToRadians(60.f);

	// Front wheel drive:
	controller_settings->mDifferentials.resize(1);
	controller_settings->mDifferentials[0].mLeftWheel = 0; // left = front wheel
	controller_settings->mDifferentials[0].mRightWheel = 1; // right = rear wheel
	controller_settings->mDifferentials[0].mLeftRightSplit = 1.f; // Apply all torque to 'right' (rear) wheel //0; // Apply all torque to 'left' (front) wheel.

	controller_settings->mEngine.mMaxTorque = 390; // Approximately the smallest value that allows wheelies.
	controller_settings->mEngine.mMaxRPM = 10000;//30000; // If only 1 gear, allow a higher max RPM
	controller_settings->mEngine.mInertia = 0.2;

	//controller_settings->mTransmission.mMode = JPH::ETransmissionMode::Manual;
	//controller_settings->mTransmission.mGearRatios = JPH::Array<float>(1, 2.66f); // Use a single forwards gear

	controller_settings->mTransmission.mShiftDownRPM = 5000.0f;
	controller_settings->mTransmission.mShiftUpRPM = 9000.0f;
	controller_settings->mTransmission.mGearRatios = { 2.27f, 1.63f, 1.3f, 1.09f, 0.96f, 0.88f }; // From: https://www.blocklayer.com/rpm-gear-bikes
	controller_settings->mTransmission.mSwitchTime = 0.2f;


	vehicle_constraint = new JPH::VehicleConstraint(*bike_body, vehicle);
	physics_world.physics_system->AddConstraint(vehicle_constraint);
	physics_world.physics_system->AddStepListener(vehicle_constraint);

	// Set the collision tester
	JPH::Ref<JPH::VehicleCollisionTester> collision_tester = new JPH::VehicleCollisionTesterCastCylinder(Layers::MOVING, /*inConvexRadiusFraction=*/1.f); // Use half wheel width as convex radius so we get a rounded cylinder
	vehicle_constraint->SetVehicleCollisionTester(collision_tester);


	// Get indices of joint nodes
	steering_node_i				= -1;
	back_arm_node_i				= -1;
	front_wheel_node_i			= -1;
	back_wheel_node_i			= -1;
	upper_piston_left_node_i	= -1;
	upper_piston_right_node_i	= -1;
	lower_piston_left_node_i	= -1;
	lower_piston_right_node_i	= -1;

	if(object->opengl_engine_ob.nonNull())
	{
		steering_node_i     = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex("Steering bone");
		back_arm_node_i     = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex("back arm bone");
		
		front_wheel_node_i  = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex("Wheel-Front");
		back_wheel_node_i  = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex("Wheel-back");

		upper_piston_left_node_i  = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex("piston upper left");
		upper_piston_right_node_i = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex("piston upper right");
	
		lower_piston_left_node_i  = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex("piston lower left");
		lower_piston_right_node_i = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex("piston lower right");
	}


	try
	{
		engine_audio_source = new glare::AudioSource();
		engine_audio_source->type = glare::AudioSource::SourceType_Streaming;
		engine_audio_source->pos = object->getCentroidWS();
		engine_audio_source->debugname = "bike engine";
		engine_audio_source->volume = 2.5f;
		engine_audio_source->mix_sources.resize(3);
		engine_audio_source->mix_sources[0].soundfile = audio_engine->getOrLoadSoundFile(base_dir_path + "/data/resources/sounds/smartsound_TRANSPORTATION_MOTORCYCLE_Engine_Slow_Idle_Steady_01_44100_hz_mono.mp3");
		engine_audio_source->mix_sources[1].soundfile = audio_engine->getOrLoadSoundFile(base_dir_path + "/data/resources/sounds/smartsound_TRANSPORTATION_MOTORCYCLE_Engine_Medium_Speed_Steady_01_44100hz_mono.mp3");
		engine_audio_source->mix_sources[2].soundfile = audio_engine->getOrLoadSoundFile(base_dir_path + "/data/resources/sounds/smartsound_TRANSPORTATION_MOTORCYCLE_Engine_High_Speed_Steady_01_44100hz_mono.mp3");
		engine_audio_source->sampling_rate = 44100;

		glare::SoundFileRef tire_squeal_sound = audio_engine->getOrLoadSoundFile(base_dir_path + "/data/resources/sounds/tires_squal_loop_44100.wav");

		wheel_audio_source[0] = new glare::AudioSource();
		wheel_audio_source[0]->type = glare::AudioSource::SourceType_Looping;
		wheel_audio_source[0]->pos = object->getCentroidWS();
		wheel_audio_source[0]->debugname = "front wheel";
		wheel_audio_source[0]->volume = 0.f;
		wheel_audio_source[0]->shared_buffer = tire_squeal_sound->buf;
	
		wheel_audio_source[1] = new glare::AudioSource();
		wheel_audio_source[1]->type = glare::AudioSource::SourceType_Looping;
		wheel_audio_source[1]->pos = object->getCentroidWS();
		wheel_audio_source[1]->debugname = "rear wheel";
		wheel_audio_source[1]->volume = 0.f;
		wheel_audio_source[1]->shared_buffer = tire_squeal_sound->buf;
		
		audio_engine->addSource(engine_audio_source);
		audio_engine->addSource(wheel_audio_source[0]);
		audio_engine->addSource(wheel_audio_source[1]);
	}
	catch(glare::Exception& e)
	{
		conPrint("Error loading bike sounds: " + e.what());
		assert(0);
	}
}


BikePhysics::~BikePhysics()
{
	if(engine_audio_source.nonNull())
	{
		m_audio_engine->removeSource(engine_audio_source);
		engine_audio_source = NULL;
	
		m_audio_engine->removeSource(wheel_audio_source[0]);
		wheel_audio_source[0] = NULL;

		m_audio_engine->removeSource(wheel_audio_source[1]);
		wheel_audio_source[1] = NULL;
	}

	m_physics_world->physics_system->RemoveConstraint(vehicle_constraint);
	m_physics_world->physics_system->RemoveStepListener(vehicle_constraint);
	vehicle_constraint = NULL;

	removeVisualisationObs();
}


void BikePhysics::vehicleSummoned() // Set engine revs to zero etc.
{
	JPH::WheeledVehicleController* controller = static_cast<JPH::WheeledVehicleController*>(vehicle_constraint->GetController());
	controller->GetEngine().SetCurrentRPM(0);
	
	vehicle_constraint->GetWheel(0)->SetAngularVelocity(0);
}


void BikePhysics::startRightingVehicle() // TEMP make abstract virtual
{
	righting_time_remaining = 2;
}


void BikePhysics::userEnteredVehicle(int seat_index) // Should set cur_seat_index
{
	assert(seat_index >= 0 && seat_index < (int)getSettings().seat_settings.size());

	if(seat_index == 0)
		user_in_driver_seat = true;

	righting_time_remaining = -1; // Stop righting vehicle
}


void BikePhysics::userExitedVehicle(int old_seat_index) // Should set cur_seat_index
{
	if(old_seat_index == 0)
		user_in_driver_seat = false;
}


VehiclePhysicsUpdateEvents BikePhysics::update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime)
{
	VehiclePhysicsUpdateEvents events;

	assert(this->bike_body_id == world_object->physics_object->jolt_body_id);

	const float speed = getLinearVel(physics_world).length();

	float forward = 0.0f, right = 0.0f, up_input = 0.f, brake = 0.0f, handbrake = 0.f;
	// Determine acceleration and brake
	if (physics_input.W_down || physics_input.up_down)
		forward = 0.5f;
	else if(physics_input.S_down || physics_input.down_down)
		forward = -0.5f;

	// Hand brake will cancel gas pedal
	if(physics_input.space_down)
	{
		brake = 1.f;
		up_input = 1.f;
	}

	if(physics_input.B_down)
	{
		handbrake = 1.f;
	}

	if(physics_input.SHIFT_down) // boost!
	{
		forward *= 2.f;
		brake *= 2.f;
	}

	if(physics_input.C_down || physics_input.CTRL_down)
		up_input = -1.f;

	/*float raw_right = 0;
	if(physics_input.A_down)
		raw_right -= 1.f;
	else if(physics_input.D_down)
		raw_right += 1.f;*/

	// Steering
	/*const float v = speed;
	const float g = 9.81f;
	const float l = 0.85f * 2; // approx
	const float max_lean_angle = JPH::DegreesToRadians(45.f);
	const float max_steer_angle_for_max_lean = std::atan(std::tan(max_lean_angle) * l * g / (v*v));
	const float max_steering_input = myClamp(max_steer_angle_for_max_lean / max_steering_angle, 0.f, 1.f);   //myClamp(20.f / speed, 0.f, 1.f);    // steering_angle = steering_input * max_steering_angle, so steering_input = steering_angle / max_steering_angle
	printVar(max_steer_angle_for_max_lean);
	printVar(max_steering_input);
	*/
	const float max_steering_input = myClamp(20.f / speed, 0.f, 1.f);
	const float steering_speed = 2.f;

	if(physics_input.A_down && !physics_input.D_down)
		cur_steering_right = myClamp(cur_steering_right - steering_speed * dtime, -max_steering_input, max_steering_input);
	else if(physics_input.D_down && !physics_input.A_down)
		cur_steering_right = myClamp(cur_steering_right + steering_speed * dtime, -max_steering_input, max_steering_input);
	else
	{
		if(cur_steering_right > 0)
			cur_steering_right = myMax(cur_steering_right - steering_speed * dtime, 0.f); // Relax to neutral steering position
		else if(cur_steering_right < 0)
			cur_steering_right = myMin(cur_steering_right + steering_speed * dtime, 0.f); // Relax to neutral steering position
	}

	right = cur_steering_right;

	
	
	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();
	
	const JPH::Mat44 transform = body_interface.GetWorldTransform(bike_body_id);

	JPH::Float4 cols[4];
	transform.StoreFloat4x4(cols);

	const Matrix4f to_world(&cols[0].x);

	// When leaned, we don't want to use the brakes fully as we'll spin out (See Jolt\Physics\Vehicle\MotorcycleController.cpp)
	if((brake > 0.0f) && !physics_input.SHIFT_down) // Don't avoid spinning out if shift key is down.
	{
		JPH::Vec3 world_up = -m_physics_world->physics_system->GetGravity().Normalized();
		JPH::Vec3 up  = transform.Multiply3x3(vehicle_constraint->GetLocalUp());
		JPH::Vec3 fwd = transform.Multiply3x3(vehicle_constraint->GetLocalForward());
		float sin_lean_angle = abs(world_up.Cross(up).Dot(fwd));
		float brake_multiplier = JPH::Square(1.0f - sin_lean_angle);
		brake *= brake_multiplier;
	}
	

	// On user input, assure that the car is active
	if (right != 0.0f || forward != 0.0f || brake != 0.0f)
		body_interface.ActivateBody(bike_body_id);


	// Pass the input on to the constraint 
	JPH::WheeledVehicleController* controller = static_cast<JPH::WheeledVehicleController*>(vehicle_constraint->GetController());
	controller->SetDriverInput(forward, right, brake, handbrake);



	// Vectors in y-forward space
	const Vec4f forwards_y_for(0,1,0,0);
	const Vec4f right_y_for(1,0,0,0);
	const Vec4f up_y_for(0,0,1,0);

	// model/object space to y-forward space = R
	// y-forward/z-up space to model/object space = R^-1

	// The particular R will depend on the space the modeller chose.

	const JPH::Quat R_quat = toJoltQuat(settings.script_settings->model_to_y_forwards_rot_2 * settings.script_settings->model_to_y_forwards_rot_1);

	const Matrix4f R_inv = ((settings.script_settings->model_to_y_forwards_rot_2 * settings.script_settings->model_to_y_forwards_rot_1).conjugate()).toMatrix();
	const Matrix4f z_up_to_model_space = R_inv;



	const Vec4f bike_forwards_os = z_up_to_model_space * forwards_y_for;
	const Vec4f bike_right_os = z_up_to_model_space * right_y_for;
	//const Vec4f bike_up_os = crossProduct(bike_right_os, bike_forwards_os);

	const Vec4f bike_right_vec_ws = to_world * bike_right_os;
	const Vec4f bike_forward_vec_ws = to_world * bike_forwards_os;

	const Vec4f up_ws = Vec4f(0,0,1,0);
	const Vec4f no_roll_vehicle_right_ws = normalise(crossProduct(bike_forward_vec_ws, up_ws));
	Vec4f no_roll_vehicle_up_ws = normalise(crossProduct(no_roll_vehicle_right_ws, bike_forward_vec_ws));
	if(dot(no_roll_vehicle_right_ws, bike_right_vec_ws) < 0)
		no_roll_vehicle_up_ws = -no_roll_vehicle_up_ws;

	if(user_in_driver_seat)
	{
		//vehicle_constraint->SetMaxRollAngle(JPH::DegreesToRadians(0.f));
		assert(dynamic_cast<JPH::MotorcycleController*>(vehicle_constraint->GetController()));
		static_cast<JPH::MotorcycleController*>(vehicle_constraint->GetController())->EnableLeanController(true);


		//TEMP make bike float for testing constraints:
		//const Vec4f up_force = Vec4f(0,0,1,0) * settings.bike_mass * 9.81;
		//body_interface.AddForce(car_body_id, toJoltVec3(up_force));


		// If both wheels are not touching anything, allow pitch control
		if(!vehicle_constraint->GetWheel(0)->HasContact() && !vehicle_constraint->GetWheel(1)->HasContact())
		{
			const Vec4f pitch_control_torque = bike_right_vec_ws * settings.bike_mass * 2.f * up_input;
			body_interface.AddTorque(bike_body_id, toJoltVec3(pitch_control_torque));
		}
		

		const bool both_wheels_on_ground = vehicle_constraint->GetWheel(0)->HasContact() && vehicle_constraint->GetWheel(1)->HasContact();
		float desired_roll_angle = 0;
		if(both_wheels_on_ground)
		{
			const Vec4f wheel_friction_lateral_impulse = toVec4fVec(
				vehicle_constraint->GetWheel(0)->GetContactLateral() * vehicle_constraint->GetWheel(0)->GetLateralLambda() + 
				vehicle_constraint->GetWheel(0)->GetContactLateral() * vehicle_constraint->GetWheel(1)->GetLateralLambda()
			);

			const float right_impulse = dot(wheel_friction_lateral_impulse, no_roll_vehicle_right_ws);

			const float use_dt = dtime;
			const float use_lateral_force = right_impulse / use_dt;

			const float N_mag = 9.81f * settings.bike_mass; // Magnitude of normal force upwards from ground
			const float f_f_mag = use_lateral_force; // friction force magnitude

			const float ratio = f_f_mag / N_mag;
			desired_roll_angle = std::atan(ratio);
		}

		/*if(!vehicle_constraint->GetWheel(0)->HasContact()) // If front wheel is not on ground:
		{
			printVar(raw_right);
			const Vec4f roll_control_torque = bike_forward_vec_ws * settings.bike_mass * 0.2f * raw_right;
			body_interface.AddTorque(bike_body_id, toJoltVec3(roll_control_torque));
		}*/

		const float lerp_factor = myMin(0.1f, dtime * 4.f);
		smoothed_desired_roll_angle = smoothed_desired_roll_angle * (1 - lerp_factor) + lerp_factor * desired_roll_angle;


		// Save vector for visualisation
	//	this->last_desired_up_vec = no_roll_vehicle_up_ws * cos(smoothed_desired_roll_angle) + no_roll_vehicle_right_ws * sin(smoothed_desired_roll_angle);

		assert(dynamic_cast<const JPH::MotorcycleController*>(vehicle_constraint->GetController()));
		//this->last_desired_up_vec = toVec4fVec(static_cast<const JPH::MotorcycleController*>(vehicle_constraint->GetController())->mTargetLean);


		//---------- Roll constraint -----------------

		// Smooth cur_target_tile_angle towards smoothed_desired_roll_angle
		// cur_target_tilt_angle = cur_target_tilt_angle * (1 - lerp_factor) + lerp_factor * smoothed_desired_roll_angle;
		cur_target_tilt_angle = smoothed_desired_roll_angle;

		//vehicle_constraint->SetTiltAngle(cur_target_tilt_angle);


		//---------- PID roll control -----------------
#if 0
		const float cur_roll_angle = atan2(dot(bike_up_vec_ws, no_roll_vehicle_right_ws), dot(bike_up_vec_ws, no_roll_vehicle_up_ws));
		//printVar(cur_roll_angle);

		//const Vec4f back_to_front_wheel_contact_points = toVec4fVec(vehicle_constraint->GetWheel(0)->GetContactPosition() - vehicle_constraint->GetWheel(1)->GetContactPosition());
			
		const float roll_err_term = smoothed_desired_roll_angle - cur_roll_angle;

		float derror_dt = (roll_err_term - last_roll_error) / dtime;
		this->last_roll_error = roll_err_term;

		const float force_signed_mag = roll_err_term * 2000.f + derror_dt * 200.f;
		const float force_mag = std::abs(force_signed_mag);
		//printVar(force_signed_mag);

		// Apply sideways force above centre of mass.
		const Vec4f force_point_ws = to_world * Vec4f(0,0,0.5f,1);
		const Vec4f side_force = bike_right_vec_ws * force_signed_mag;
		//body_interface.AddForce(bike_body_id, toJoltVec3(side_force), toJoltVec3(force_point_ws));

		//body_interface.AddForce(bike_body_id, toJoltVec3(up_ws * force_mag * -0.2)); // Apply force keeping bike on ground.


		last_force_point = force_point_ws; // Save for visualisation
		last_force_vec = side_force;
#endif


		// conPrint("RPM: " + doubleToStringNDecimalPlaces(controller->GetEngine().GetCurrentRPM(), 1));
		// conPrint("engine torque: " + doubleToStringNDecimalPlaces(controller->GetEngine().GetTorque(forward), 1));
		// conPrint("current gear: " + toString(controller->GetTransmission().GetCurrentGear()));

	}
	else // Else if cur_seat_index != 0:
	{
		//vehicle_constraint->SetMaxRollAngle(JPH::JPH_PI); // user is not on bike, so deactivate roll constraint.
		assert(dynamic_cast<JPH::MotorcycleController*>(vehicle_constraint->GetController()));
		static_cast<JPH::MotorcycleController*>(vehicle_constraint->GetController())->EnableLeanController(false);
	}

	
	if(righting_time_remaining > 0) // If currently righting bike:
	{
		// Get current rotation, compute the desired rotation, which is a rotation with the current yaw but no pitch or roll, 
		// compute a rotation to get from current to desired
		const JPH::Quat current_rot = body_interface.GetRotation(bike_body_id);

		const float current_yaw_angle = std::atan2(no_roll_vehicle_right_ws[1], no_roll_vehicle_right_ws[0]); // = rotation of right vector around the z vector

		const JPH::Quat desired_rot = JPH::Quat::sRotation(JPH::Vec3(0,0,1), current_yaw_angle) * R_quat;

		const JPH::Quat cur_to_desired_rot = desired_rot * current_rot.Conjugated();
		JPH::Vec3 axis;
		float angle;
		cur_to_desired_rot.GetAxisAngle(axis, angle);

		// Choose a desired angular velocity which is proportional in magnitude to how far we need to rotate.
		// Note that we can't just apply a constant torque in the desired rotation direction, or we get angular oscillations around the desired rotation.
		const JPH::Vec3 desired_angular_vel = (axis * angle) * 3;

		// Apply a torque to try and match the desired angular velocity.
		const JPH::Vec3 angular_vel = body_interface.GetAngularVelocity(bike_body_id);
		const JPH::Vec3 correction_torque = (desired_angular_vel - angular_vel) * settings.bike_mass * 3.5f;
		body_interface.AddTorque(bike_body_id, correction_torque);

		righting_time_remaining -= dtime;
	}

	
	//const float speed_km_h = getLinearVel(physics_world).length() * (3600.0f / 1000.f);
	//conPrint("speed (km/h): " + doubleToStringNDecimalPlaces(speed_km_h, 1));


	// Set bike joint node transforms
	GLObject* graphics_ob = world_object->opengl_engine_ob.ptr();
	if(graphics_ob)
	{
		const Vec4f steering_axis = Vec4f(0,1,0,0);

		if(steering_node_i >= 0 && steering_node_i < (int)graphics_ob->anim_node_data.size())
		{
			const float steering_angle = vehicle_constraint->GetWheel(0)->GetSteerAngle();
			graphics_ob->anim_node_data[steering_node_i].procedural_transform = Matrix4f::rotationMatrix(steering_axis, -steering_angle);
		}


		if(back_arm_node_i >= 0 && back_arm_node_i < (int)graphics_ob->anim_node_data.size())
		{
			const float sus_len = vehicle_constraint->GetWheel(1)->GetSuspensionLength();

			Vec4f to_pivot_trans(0,0,1.35,0);
			graphics_ob->anim_node_data[back_arm_node_i].procedural_transform = Matrix4f::rotationAroundXAxis((sus_len - 0.225f) * 3);
		}

		// Front wheel
		if(front_wheel_node_i >= 0 && front_wheel_node_i < (int)graphics_ob->anim_node_data.size())
		{
			const float front_sus_len = vehicle_constraint->GetWheel(0)->GetSuspensionLength();
			const Vec4f translation_dir = normalise(Vec4f(0, 2.37f, 1.87f,0)); // y is upwards
			const float suspension_offset = 0.29f;
			const Vec4f translation = translation_dir * -(world_to_ob_scale * (front_sus_len /** 1.5f*/ - suspension_offset)); // 1.5 to compensate for angle of suspension
			graphics_ob->anim_node_data[front_wheel_node_i].procedural_transform = Matrix4f::translationMatrix(translation) * Matrix4f::rotationAroundXAxis(-vehicle_constraint->GetWheel(0)->GetRotationAngle());
		}

		// Back wheel
		if(back_wheel_node_i >= 0 && back_wheel_node_i < (int)graphics_ob->anim_node_data.size())
		{
			graphics_ob->anim_node_data[back_wheel_node_i].procedural_transform = Matrix4f::rotationAroundXAxis(-vehicle_constraint->GetWheel(1)->GetRotationAngle());
		}

		// Upper piston rotation
		if( upper_piston_left_node_i  >= 0 && upper_piston_left_node_i  < (int)graphics_ob->anim_node_data.size() && 
			upper_piston_right_node_i >= 0 && upper_piston_right_node_i < (int)graphics_ob->anim_node_data.size())
		{
			const float sus_len = vehicle_constraint->GetWheel(1)->GetSuspensionLength();

			// HACK: approximate rotation angle with affine function, also clamp max angle or it looks silly.
			// Proper solution is to solve for angle based on back arm angle, point on back arm etc.
			const float max_rot = 0.02f;
			const float rot = myMin(max_rot, (sus_len - 0.26f) * 0.7f);

			graphics_ob->anim_node_data[upper_piston_left_node_i ].procedural_transform = Matrix4f::rotationAroundXAxis(rot);
			graphics_ob->anim_node_data[upper_piston_right_node_i].procedural_transform = Matrix4f::rotationAroundXAxis(rot);
		}


		// Lower piston compression
		if( lower_piston_left_node_i  >= 0 && lower_piston_left_node_i  < (int)graphics_ob->anim_node_data.size() && 
			lower_piston_right_node_i >= 0 && lower_piston_right_node_i < (int)graphics_ob->anim_node_data.size())
		{
			const float sus_len = vehicle_constraint->GetWheel(1)->GetSuspensionLength();

			const float length_scale = 1.f + (sus_len - 0.23f) * 0.7f; // TEMP HACK approximate spring length scale with an affine function.
			const float offset = -(sus_len - 0.23f) * 0.7f; // Scaling keeps the bottom of the spring fixed, we want the top part of the spring fixed, so offset it up based on scale.

			graphics_ob->anim_node_data[lower_piston_left_node_i].procedural_transform  = Matrix4f::scaleMatrix(1, length_scale, 1) * Matrix4f::translationMatrix(0, offset, 0);
			graphics_ob->anim_node_data[lower_piston_right_node_i].procedural_transform = Matrix4f::scaleMatrix(1, length_scale, 1) * Matrix4f::translationMatrix(0, offset, 0);
		}
	}




	// ---------------------------------- audio -----------------------------------
	if(engine_audio_source.nonNull()) // If bike audio was initialised:
	{
		// Set mix parameters for the engine sound.  Actual mixing will get done in the ResonanceThread.
		{
			Lock lock(m_audio_engine->mutex);

			const float current_RPM = controller->GetEngine().GetCurrentRPM();
			const float cur_engine_freq = current_RPM / 60.f;
			//printVar(current_RPM);

			const float low_source_freq = 43.f / 2; // From Audacity spectrum analysis
			const float low_source_delta = engine_audio_source->doppler_factor * cur_engine_freq / low_source_freq * ((float)/*engine_low_audio_sound_file->sample_rate*/48000.f / m_audio_engine->getSampleRate());

			const float mid_source_freq = 72.f;
			const float mid_source_delta = engine_audio_source->doppler_factor * cur_engine_freq / mid_source_freq * ((float)/*engine_mid_audio_sound_file->sample_rate*/48000.f / m_audio_engine->getSampleRate());

			const float high_source_freq = 122.f;
			const float high_source_delta = engine_audio_source->doppler_factor * cur_engine_freq / high_source_freq * ((float)/*engine_high_audio_sound_file->sample_rate*/48000.f / m_audio_engine->getSampleRate());

			//printVar(mid_source_delta);
			//printVar(low_source_delta);
			//printVar(high_source_delta);

			float high_intensity_factor = 0.9f * Maths::smoothStep(mid_source_freq * 0.6f, high_source_freq, cur_engine_freq);
			float low_intensity_factor = (1 - Maths::smoothStep(low_source_freq, mid_source_freq, cur_engine_freq) * 0.8f) * (1 - high_intensity_factor);
			float mid_intensity_factor = (1 - low_intensity_factor) * (1 - high_intensity_factor);
			
			// Intensity = amplitude^2, amplitude = sqrt(intensity)
			float low_factor  = std::sqrt(myMax(0.f, low_intensity_factor));
			float mid_factor  = std::sqrt(myMax(0.f, mid_intensity_factor)); 
			float high_factor = std::sqrt(myMax(0.f, high_intensity_factor));

			//printVar(low_intensity_factor);
			//printVar(mid_intensity_factor);
			//printVar(high_intensity_factor);
			//printVar(doppler_factor);

			engine_audio_source->mix_sources[0].source_delta = low_source_delta;
			engine_audio_source->mix_sources[1].source_delta = mid_source_delta;
			engine_audio_source->mix_sources[2].source_delta = high_source_delta;

			engine_audio_source->mix_sources[0].mix_factor = low_factor;
			engine_audio_source->mix_sources[1].mix_factor = mid_factor;
			engine_audio_source->mix_sources[2].mix_factor = high_factor;
		}

		engine_audio_source->pos = to_world * Vec4f(0,0,0,1);
		m_audio_engine->sourcePositionUpdated(*engine_audio_source);
	}

	// Set volume for tire squeal sounds, do tire-skid smoke/dust particle effects
	for(int i=0; i<2; ++i)
	{
		if(vehicle_constraint->GetWheel(i)->HasContact())
		{
			// See WheelWV::Update() in Jolt\Physics\Vehicle\WheeledVehicleController.cpp
			JPH::Vec3 relative_velocity = body_interface.GetPointVelocity(bike_body_id, vehicle_constraint->GetWheel(i)->GetContactPosition()) - vehicle_constraint->GetWheel(i)->GetContactPointVelocity();

			// Cancel relative velocity in the normal plane
			relative_velocity -= vehicle_constraint->GetWheel(i)->GetContactNormal().Dot(relative_velocity) * vehicle_constraint->GetWheel(i)->GetContactNormal();

			const float relative_longitudinal_velocity = relative_velocity.Dot(vehicle_constraint->GetWheel(i)->GetContactLongitudinal());

			const float longitudinal_vel_diff = vehicle_constraint->GetWheel(i)->GetAngularVelocity() * wheel_radius - relative_longitudinal_velocity;

			const float relative_lateral_vel = relative_velocity.Dot(vehicle_constraint->GetWheel(i)->GetContactLateral());

			const float sidewards_skid_speed = std::fabs(relative_lateral_vel);
			const float forwards_skid_speed = std::fabs(longitudinal_vel_diff); 
			const float skid_speed = forwards_skid_speed + sidewards_skid_speed;

			// Compute wheel contact position.  Use getBodyTransform and GetWheelLocalTransform instead of GetContactPosition() since that lags behind the actual contact position a bit.
			//const Vec4f contact_point_ws = toVec4fPos(vehicle_constraint->GetWheel(i)->GetContactPosition()); // getBodyTransform(*m_physics_world) * (wheel_to_local_transform * Vec4f(0, 0, 0, 1) - toVec4fVec(wheel_up_os) * wheel_radius);
			JPH::Vec3 wheel_forward_os, wheel_up_os, wheel_right_os;
			vehicle_constraint->GetWheelLocalBasis(vehicle_constraint->GetWheel(i), wheel_forward_os, wheel_up_os, wheel_right_os);
			const Matrix4f wheel_to_local_transform = toMatrix4f(vehicle_constraint->GetWheelLocalTransform(i, /*inWheelRight=*/JPH::Vec3::sAxisZ(), /*inWheelUp=*/JPH::Vec3::sAxisX()));
			
			const Vec4f contact_point_ws = to_world * (wheel_to_local_transform * Vec4f(0, 0, 0, 1) - toVec4fVec(wheel_up_os) * wheel_radius);

			// Spawn some smoke particles if skidding
			if(skid_speed > 1.f)
			{
				Particle particle;
				particle.pos = contact_point_ws;
				particle.area = 0.001f;
				particle.vel = Vec4f(-1 + 2*rng.unitRandom(),-1 + 2*rng.unitRandom(), rng.unitRandom() * 3, 0) * 2.f;
				particle.colour = Colour3f(0.5f);
				particle.cur_opacity = 0.4f;
				particle.dopacity_dt = -0.1f;
				particle.particle_type = Particle::ParticleType_Smoke;
				particle.width = 0.75f;
				particle.theta = rng.unitRandom() * Maths::get2Pi<float>();
				particle_manager->addParticle(particle);
			}

			const float skid_volume = myMin(3.f, skid_speed * 1.f);

			if(wheel_audio_source->nonNull())
			{
				wheel_audio_source[i]->pos = contact_point_ws;
				if(wheel_audio_source[i]->volume != skid_volume)
				{
					wheel_audio_source[i]->volume = skid_volume;
					m_audio_engine->sourceVolumeUpdated(*wheel_audio_source[i]);
				}
				m_audio_engine->sourcePositionUpdated(*wheel_audio_source[i]);
			}
		}
		else // Else if wheel is not in contact with ground, set volume to zero.
		{
			if(wheel_audio_source[i].nonNull())
				if(wheel_audio_source[i]->volume != 0.f)
				{
					wheel_audio_source[i]->volume = 0.f;
					m_audio_engine->sourceVolumeUpdated(*wheel_audio_source[i]);
				}
		}
	}

	return events;
}


Vec4f BikePhysics::getFirstPersonCamPos(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const
{
	const Matrix4f seat_to_world = getSeatToWorldTransform(physics_world, seat_index, use_smoothed_network_transform);
	return seat_to_world * Vec4f(0,0,0.6f,1); // Raise camera position to appox head position
}


Vec4f BikePhysics::getThirdPersonCamTargetTranslation() const
{
	return Vec4f(0, 0, 0, 0);
}


Matrix4f BikePhysics::getBodyTransform(PhysicsWorld& physics_world) const
{
	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();
	const JPH::Mat44 transform = body_interface.GetWorldTransform(bike_body_id);

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
Matrix4f BikePhysics::getSeatToWorldTransform(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const
{ 
	if(seat_index < settings.script_settings->seat_settings.size())
	{
		const Matrix4f R_inv = ((settings.script_settings->model_to_y_forwards_rot_2 * settings.script_settings->model_to_y_forwards_rot_1).conjugate()).toMatrix();

		Matrix4f ob_to_world_no_scale;
		if(use_smoothed_network_transform && world_object->physics_object.nonNull())
			ob_to_world_no_scale = world_object->physics_object->getSmoothedObToWorldNoScaleMatrix();
		else
			ob_to_world_no_scale = getBodyTransform(physics_world);

		// Seat to world = object to world * seat to object
		return ob_to_world_no_scale * Matrix4f::translationMatrix(settings.script_settings->seat_settings[seat_index].seat_position) * R_inv;
	}
	else
	{
		assert(0);
		return Matrix4f::identity();
	}
}


Vec4f BikePhysics::getLinearVel(PhysicsWorld& physics_world) const
{
	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();
	return toVec4fVec(body_interface.GetLinearVelocity(bike_body_id));
}


void BikePhysics::updateDopplerEffect(const Vec4f& listener_linear_vel, const Vec4f& listener_pos)
{
	if(engine_audio_source.nonNull())
		engine_audio_source->updateDopplerEffectFactor(
			getLinearVel(*m_physics_world), // source linear vel,
			listener_linear_vel, listener_pos
		);
}


std::string BikePhysics::getUIInfoMsg()
{
	const float speed_km_per_h = getLinearVel(*m_physics_world).length() * (3600.0f / 1000.f);

	assert(dynamic_cast<const JPH::MotorcycleController*>(vehicle_constraint->GetController()));
	//this->last_desired_up_vec = toVec4fVec(static_cast<const JPH::MotorcycleController*>(vehicle_constraint->GetController())->mTargetLean);


	const float target_lean_angle_deg = 0;//JPH::RadiansToDegrees(std::acos(static_cast<const JPH::MotorcycleController*>(vehicle_constraint->GetController())->mTargetLean.Normalized().GetZ()));

	const float actual_lean_angle_deg = JPH::RadiansToDegrees(std::acos(normalise(getBodyTransform(*m_physics_world) * Vec4f(0,1,0,0))[2]));

	return doubleToStringMaxNDecimalPlaces(speed_km_per_h, 0) + " km/h, target_lean_angle: " + doubleToStringNDecimalPlaces(target_lean_angle_deg, 1) + 
		", actual_lean_angle: " + doubleToStringNDecimalPlaces(actual_lean_angle_deg, 1);
}


void BikePhysics::updateDebugVisObjects(OpenGLEngine& opengl_engine, bool should_show)
{
	m_opengl_engine = &opengl_engine;

	if(should_show)
	{
		const Matrix4f R_inv = ((settings.script_settings->model_to_y_forwards_rot_2 * settings.script_settings->model_to_y_forwards_rot_1).conjugate()).toMatrix();
		const Matrix4f z_up_to_model_space = R_inv;

		//------------------ body ------------------
		if(body_gl_ob.isNull())
		{
			body_gl_ob = opengl_engine.makeAABBObject(Vec4f(-half_vehicle_width, -half_vehicle_length, -half_vehicle_height, 1), Vec4f(half_vehicle_width, half_vehicle_length, half_vehicle_height, 1), Colour4f(1,0,0,0.2));
			opengl_engine.addObject(body_gl_ob);
		}

		body_gl_ob->ob_to_world_matrix = getBodyTransform(*m_physics_world) * z_up_to_model_space * 
			OpenGLEngine::AABBObjectTransform( // AABB in z-up space
				Vec4f(-half_vehicle_width, -half_vehicle_length, -half_vehicle_height, 1),
				Vec4f(half_vehicle_width, half_vehicle_length, half_vehicle_height, 1)
			);
		opengl_engine.updateObjectTransformData(*body_gl_ob);

		//------------------ wheels ------------------
		//for(int i=0; i<2; ++i)
		//{
		//	if(wheel_gl_ob[i].isNull())
		//	{
		//		wheel_gl_ob[i] = opengl_engine.makeSphereObject(wheel_radius, Colour4f(0,1,0,0.2));
		//		opengl_engine.addObject(wheel_gl_ob[i]);
		//	}
		//
		//	wheel_gl_ob[i]->ob_to_world_matrix = getWheelToWorldTransform(*m_physics_world, /*wheel index=*/i) * Matrix4f::uniformScaleMatrix(wheel_radius);
		//
		//	opengl_engine.updateObjectTransformData(*wheel_gl_ob[i]);
		//}

		//------------------ convex hull points ------------------
		{
			convex_hull_pts_gl_obs.resize(convex_hull_pts.size());
			for(size_t i=0; i<convex_hull_pts_gl_obs.size(); ++i)
			{
				const float radius = 0.03f;
				if(convex_hull_pts_gl_obs[i].isNull())
				{
					convex_hull_pts_gl_obs[i] = opengl_engine.makeSphereObject(radius, Colour4f(1,0,0,0.5));
					opengl_engine.addObject(convex_hull_pts_gl_obs[i]);
				}

				convex_hull_pts_gl_obs[i]->ob_to_world_matrix = getBodyTransform(*m_physics_world) * Matrix4f::translationMatrix(toVec4fPos(convex_hull_pts[i])) * Matrix4f::uniformScaleMatrix(radius);

				opengl_engine.updateObjectTransformData(*convex_hull_pts_gl_obs[i]);
			}
		}

		//------------------ suspension attachment point ------------------
		for(int i=0; i<2; ++i)
		{
			const float radius = 0.03f;
			if(wheel_attach_point_gl_ob[i].isNull())
			{
				wheel_attach_point_gl_ob[i] = opengl_engine.makeSphereObject(radius, Colour4f(0,0,1,0.5));
				opengl_engine.addObject(wheel_attach_point_gl_ob[i]);
			}

			wheel_attach_point_gl_ob[i]->ob_to_world_matrix = getBodyTransform(*m_physics_world) * Matrix4f::translationMatrix(toVec4fVec(vehicle_constraint->GetWheel(i)->GetSettings()->mPosition)) * Matrix4f::uniformScaleMatrix(radius);

			opengl_engine.updateObjectTransformData(*wheel_attach_point_gl_ob[i]);
		}
	
		//------------------ wheel-ground contact point ------------------
		for(int i=0; i<2; ++i)
		{
			const float radius = 0.03f;
			if(contact_point_gl_ob[i].isNull())
			{
				contact_point_gl_ob[i] = opengl_engine.makeSphereObject(radius, Colour4f(0,1,0,1));
				opengl_engine.addObject(contact_point_gl_ob[i]);
			}

			if(vehicle_constraint->GetWheel(i)->HasContact())
				contact_point_gl_ob[i]->ob_to_world_matrix = Matrix4f::translationMatrix(toVec4fVec(vehicle_constraint->GetWheel(i)->GetContactPosition())) * Matrix4f::uniformScaleMatrix(radius);
			else
				contact_point_gl_ob[i]->ob_to_world_matrix = Matrix4f::translationMatrix(0,0,-1000); // hide

			opengl_engine.updateObjectTransformData(*contact_point_gl_ob[i]);
		}
	
		const float FORCE_VECTOR_SCALE = 0.2f;
		//------------------ wheel-ground lateral force vectors ------------------
		for(int i=0; i<2; ++i)
		{
			if(contact_laterial_force_gl_ob[i].isNull())
			{
				contact_laterial_force_gl_ob[i] = opengl_engine.makeArrowObject(Vec4f(0,0,0,1), Vec4f(1,0,0,1), Colour4f(0.6,0.6,0,1), 1.f);
				opengl_engine.addObject(contact_laterial_force_gl_ob[i]);
			}

			if(vehicle_constraint->GetWheel(i)->HasContact() && std::fabs(vehicle_constraint->GetWheel(i)->GetLateralLambda()) > 1.0e-3f)
			{
				const Vec4f arrow_origin = toVec4fPos(vehicle_constraint->GetWheel(i)->GetContactPosition()) + Vec4f(0,0,0.02f,0); // raise off ground a little to see more easily.
				contact_laterial_force_gl_ob[i]->ob_to_world_matrix = OpenGLEngine::arrowObjectTransform(
					arrow_origin, 
					arrow_origin + toVec4fVec(vehicle_constraint->GetWheel(i)->GetContactLateral() * vehicle_constraint->GetWheel(i)->GetLateralLambda() * FORCE_VECTOR_SCALE), 1.f);
			}
			else
				contact_laterial_force_gl_ob[i]->ob_to_world_matrix = Matrix4f::translationMatrix(0,0,-1000); // hide

			opengl_engine.updateObjectTransformData(*contact_laterial_force_gl_ob[i]);
		}
		//------------------ wheel-ground suspension force vectors ------------------
		for(int i=0; i<2; ++i)
		{
			if(contact_suspension_force_gl_ob[i].isNull())
			{
				contact_suspension_force_gl_ob[i] = opengl_engine.makeArrowObject(Vec4f(0,0,0,1), Vec4f(1,0,0,1), Colour4f(0.6,0.6,0,1), 1.f);
				opengl_engine.addObject(contact_suspension_force_gl_ob[i]);
			}

			if(vehicle_constraint->GetWheel(i)->HasContact() && std::fabs(vehicle_constraint->GetWheel(i)->GetSuspensionLambda()) > 1.0e-3f)
			{
				const Vec4f arrow_origin = toVec4fPos(vehicle_constraint->GetWheel(i)->GetContactPosition());
				contact_suspension_force_gl_ob[i]->ob_to_world_matrix = OpenGLEngine::arrowObjectTransform(
					arrow_origin, 
					arrow_origin + toVec4fVec(vehicle_constraint->GetWheel(i)->GetContactNormal() * vehicle_constraint->GetWheel(i)->GetSuspensionLambda() * FORCE_VECTOR_SCALE), 1.f);
			}
			else
				contact_suspension_force_gl_ob[i]->ob_to_world_matrix = Matrix4f::translationMatrix(0,0,-1000); // hide

			opengl_engine.updateObjectTransformData(*contact_suspension_force_gl_ob[i]);
		}
	
		//------------------ wheels (wheel collision tester cylinder) ------------------
		for(int i=0; i<2; ++i)
		{
			const float radius = wheel_radius;
			if(coll_tester_gl_ob[i].isNull())
			{
				coll_tester_gl_ob[i] = opengl_engine.makeCylinderObject(radius, Colour4f(0,0,1,0.5)); // A cylinder from (0,0,0), to (0,0,1) with radius 1;
				opengl_engine.addObject(coll_tester_gl_ob[i]);
			}

			const Matrix4f wheel_to_world_transform = toMatrix4f(vehicle_constraint->GetWheelWorldTransform(i, /*inWheelRight=*/JPH::Vec3::sAxisZ(), /*inWheelUp=*/JPH::Vec3::sAxisX()));

			coll_tester_gl_ob[i]->ob_to_world_matrix = 
				wheel_to_world_transform *
				Matrix4f::scaleMatrix(radius, radius, vehicle_constraint->GetWheel(i)->GetSettings()->mWidth) * // scale cylinder
				Matrix4f::translationMatrix(0,0,-0.5f); // centre cylinder around origin

			opengl_engine.updateObjectTransformData(*coll_tester_gl_ob[i]);
		}
	
		//------------------ Visualise righting force with arrow ------------------
		if(false)
		{
			if(righting_force_gl_ob.isNull())
			{
				righting_force_gl_ob = opengl_engine.makeArrowObject(Vec4f(0,0,0,1), Vec4f(1,0,0,0), Colour4f(0,0,1,0.5), 0.05f);
				opengl_engine.addObject(righting_force_gl_ob);
			}

			if(last_force_vec.length() > 1.0e-3f)
			{
				righting_force_gl_ob->ob_to_world_matrix = OpenGLEngine::arrowObjectTransform(/*startpos=*/last_force_point, /*endpos=*/last_force_point + last_force_vec * 0.001f, 1.f);
			}
			else
			{
				righting_force_gl_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0,0,-1000); // hide
			}

			opengl_engine.updateObjectTransformData(*righting_force_gl_ob);
		}
	
		//------------------ Visualise desired roll angle with arrow ------------------
		{
			if(desired_bike_up_vec_gl_ob.isNull())
			{
				desired_bike_up_vec_gl_ob = opengl_engine.makeArrowObject(Vec4f(0,0,0,1), Vec4f(1,0,0,1), Colour4f(0,0,1,0.5), 1.f);
				opengl_engine.addObject(desired_bike_up_vec_gl_ob);
			}

			if(last_desired_up_vec.length() > 1.0e-3f)
			{
				const Vec4f bike_pos_ws = getBodyTransform(*m_physics_world) * Vec4f(0,0,0,1);

				desired_bike_up_vec_gl_ob->ob_to_world_matrix = //Matrix4f::translationMatrix(bike_pos_ws) * //Matrix4f::constructFromVectorXAxis(normalise(last_desired_up_vec)) * Matrix4f::scaleMatrix(last_desired_up_vec.length(), 1, 1);
					OpenGLEngine::arrowObjectTransform(/*startpos=*/bike_pos_ws, /*endpos=*/bike_pos_ws + last_desired_up_vec, /*radius scale=*/1.f);
			}

			opengl_engine.updateObjectTransformData(*desired_bike_up_vec_gl_ob);
		}
	}
	else
	{
		removeVisualisationObs();
	}
}


void BikePhysics::removeVisualisationObs()
{
	if(m_opengl_engine)
	{
		if(body_gl_ob.nonNull())
			m_opengl_engine->removeObject(body_gl_ob);
		body_gl_ob = NULL;

		for(int i=0; i<2; ++i)
		{
			if(wheel_attach_point_gl_ob[i].nonNull())
				m_opengl_engine->removeObject(wheel_attach_point_gl_ob[i]);
			wheel_attach_point_gl_ob[i] = NULL;

			if(wheel_gl_ob[i].nonNull())
				m_opengl_engine->removeObject(wheel_gl_ob[i]);
			wheel_gl_ob[i] = NULL;

			if(coll_tester_gl_ob[i].nonNull())
				m_opengl_engine->removeObject(coll_tester_gl_ob[i]);
			coll_tester_gl_ob[i] = NULL;

			if(contact_point_gl_ob[i].nonNull())
				m_opengl_engine->removeObject(contact_point_gl_ob[i]);
			contact_point_gl_ob[i] = NULL;

			if(contact_laterial_force_gl_ob[i].nonNull())
				m_opengl_engine->removeObject(contact_laterial_force_gl_ob[i]);
			contact_laterial_force_gl_ob[i] = NULL;

			if(contact_suspension_force_gl_ob[i].nonNull())
				m_opengl_engine->removeObject(contact_suspension_force_gl_ob[i]);
			contact_suspension_force_gl_ob[i] = NULL;
		}

		if(righting_force_gl_ob.nonNull())
			m_opengl_engine->removeObject(righting_force_gl_ob);
		righting_force_gl_ob = NULL;

		if(desired_bike_up_vec_gl_ob.nonNull())
			m_opengl_engine->removeObject(desired_bike_up_vec_gl_ob);
		desired_bike_up_vec_gl_ob = NULL;

		for(size_t i=0; i<convex_hull_pts_gl_obs.size(); ++i)
			m_opengl_engine->removeObject(convex_hull_pts_gl_obs[i]);
		convex_hull_pts_gl_obs.clear();
	}
}

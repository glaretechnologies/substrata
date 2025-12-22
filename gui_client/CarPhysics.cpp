/*=====================================================================
CarPhysics.cpp
--------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "CarPhysics.h"


#include "CameraController.h"
#include "PhysicsWorld.h"
#include "PhysicsObject.h"
#include "ParticleManager.h"
#include "JoltUtils.h"
#include <StringUtils.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <opengl/OpenGLEngine.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>


static const float ob_to_world_scale = 1.f;//1.f / 0.014f;


CarPhysics::CarPhysics(WorldObjectRef object, JPH::BodyID car_body_id_, CarPhysicsSettings settings_, PhysicsWorld& physics_world, 
	glare::AudioEngine* audio_engine, const std::string& base_dir_path, ParticleManager* particle_manager_)
:	m_opengl_engine(NULL),
	particle_manager(particle_manager_),
	show_debug_vis_obs(false)
{
	car_body_id = car_body_id_;

	world_object = object.ptr();
	m_physics_world = &physics_world;

	user_in_driver_seat = false;

	cur_steering_right = 0;

	settings = settings_;
	righting_time_remaining = -1;


	cur_steering_right = 0;

	const Matrix4f z_up_to_model_space = ((settings.script_settings->model_to_y_forwards_rot_2 * settings.script_settings->model_to_y_forwards_rot_1).conjugate()).toMatrix();

	const Vec4f cur_pos = object->physics_object->pos;
	const Quatf cur_rot = object->physics_object->rot;

	// Remove existing car physics object
	physics_world.removeObject(object->physics_object);

	// Create collision tester
	m_tester = new JPH::VehicleCollisionTesterCastSphere(Layers::MOVING, 0.5f * settings.script_settings->front_wheel_width, /*inUp=*/JPH::Vec3(0,0,1));

	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();

	// Create vehicle body
	convex_hull_pts.resize(settings.script_settings->convex_hull_points.size());
	for(size_t i=0; i<settings.script_settings->convex_hull_points.size(); ++i)
		convex_hull_pts[i] = toJoltVec3(settings.script_settings->convex_hull_points[i]);

	JPH::Ref<JPH::ConvexHullShapeSettings> hull_shape_settings = new JPH::ConvexHullShapeSettings(convex_hull_pts);
	JPH::Ref<JPH::Shape> convex_hull_shape = hull_shape_settings->Create().Get();

	JPH::Ref<JPH::Shape> car_body_shape = JPH::OffsetCenterOfMassShapeSettings(toJoltVec3(object->centre_of_mass_offset_os),
		convex_hull_shape
	).Create().Get();

	// Create vehicle body
	JPH::BodyCreationSettings car_body_settings(car_body_shape, toJoltVec3(cur_pos), toJoltQuat(cur_rot), JPH::EMotionType::Dynamic, Layers::VEHICLES);
	car_body_settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
	car_body_settings.mMassPropertiesOverride.mMass = object->mass;
	car_body_settings.mUserData = (uint64)object->physics_object.ptr();
	jolt_body = body_interface.CreateBody(car_body_settings);

	this->car_body_id                    = jolt_body->GetID();
	object->physics_object->jolt_body_id = jolt_body->GetID();

	body_interface.AddBody(jolt_body->GetID(), JPH::EActivation::Activate);

	physics_world.addObject(object->physics_object);


	// Create vehicle constraint
	JPH::VehicleConstraintSettings vehicle;
	vehicle.mUp = toJoltVec3(z_up_to_model_space * Vec4f(0,0,1,0));
	vehicle.mForward = toJoltVec3(z_up_to_model_space * Vec4f(0,1,0,0));

	// Wheels

	const Vec4f steering_axis_z_up = normalise(Vec4f(0, 0, 1, 0)); // = front suspension dir

	// TEMP: print out animation_data node names
	// for(size_t i=0; i<object->opengl_engine_ob->mesh_data->animation_data.nodes.size(); ++i)
	// 	conPrint("Node " + toString(i) + ": " + object->opengl_engine_ob->mesh_data->animation_data.nodes[i].name);


	const Vec4f front_left_wheel_pos_ms  = object->opengl_engine_ob->mesh_data->animation_data.getNodePositionModelSpace(settings.script_settings->front_left_wheel_joint_name,  false);
	const Vec4f front_right_wheel_pos_ms = object->opengl_engine_ob->mesh_data->animation_data.getNodePositionModelSpace(settings.script_settings->front_right_wheel_joint_name, false);
	const Vec4f back_left_wheel_pos_ms   = object->opengl_engine_ob->mesh_data->animation_data.getNodePositionModelSpace(settings.script_settings->back_left_wheel_joint_name,   false);
	const Vec4f back_right_wheel_pos_ms  = object->opengl_engine_ob->mesh_data->animation_data.getNodePositionModelSpace(settings.script_settings->back_right_wheel_joint_name,  false);

	const float max_brake_torque = settings.script_settings->max_brake_torque;
	const float max_handbrake_torque = settings.script_settings->max_handbrake_torque;

	JPH::WheelSettingsWV* w1 = new JPH::WheelSettingsWV;
	w1->mPosition = toJoltVec3(front_left_wheel_pos_ms + z_up_to_model_space * Vec4f(0, 0, settings.script_settings->front_suspension_min_length + settings.script_settings->front_wheel_attachment_point_raise_dist, 0));
	w1->mSuspensionDirection	= toJoltVec3(z_up_to_model_space * -steering_axis_z_up); // Direction of the suspension in local space of the body
	w1->mSteeringAxis			= toJoltVec3(z_up_to_model_space *  steering_axis_z_up);
	w1->mWheelUp				= toJoltVec3(z_up_to_model_space *  steering_axis_z_up);
	w1->mWheelForward			= toJoltVec3(z_up_to_model_space * Vec4f(0,1,0,0));
	w1->mWidth = settings.script_settings->front_wheel_width;
	w1->mSuspensionSpring.mFrequency = settings.script_settings->front_suspension_spring_freq;
	w1->mSuspensionSpring.mDamping   = settings.script_settings->front_suspension_spring_damping;
	w1->mMaxSteerAngle = settings.script_settings->max_steering_angle;
	w1->mMaxBrakeTorque = max_brake_torque;
	w1->mMaxHandBrakeTorque = 0.0f; // Front wheel doesn't have hand brake

	JPH::WheelSettingsWV* w2 = new JPH::WheelSettingsWV;
	w2->mPosition = toJoltVec3(front_right_wheel_pos_ms + z_up_to_model_space * Vec4f(0, 0, settings.script_settings->front_suspension_min_length + settings.script_settings->front_wheel_attachment_point_raise_dist, 0));
	w2->mSuspensionDirection	= toJoltVec3(z_up_to_model_space * -steering_axis_z_up); // Direction of the suspension in local space of the body
	w2->mSteeringAxis			= toJoltVec3(z_up_to_model_space *  steering_axis_z_up);
	w2->mWheelUp				= toJoltVec3(z_up_to_model_space *  steering_axis_z_up);
	w2->mWheelForward			= toJoltVec3(z_up_to_model_space * Vec4f(0,1,0,0));
	w2->mWidth = settings.script_settings->front_wheel_width;
	w2->mSuspensionSpring.mFrequency = settings.script_settings->front_suspension_spring_freq;
	w2->mSuspensionSpring.mDamping   = settings.script_settings->front_suspension_spring_damping;
	w2->mMaxSteerAngle = settings.script_settings->max_steering_angle;
	w2->mMaxBrakeTorque = max_brake_torque;
	w2->mMaxHandBrakeTorque = 0.0f; // Front wheel doesn't have hand brake

	JPH::WheelSettingsWV* w3 = new JPH::WheelSettingsWV;
	w3->mPosition = toJoltVec3(back_left_wheel_pos_ms + z_up_to_model_space * Vec4f(0, 0, settings.script_settings->rear_suspension_min_length + settings.script_settings->rear_wheel_attachment_point_raise_dist, 0));
	w3->mSuspensionDirection	= toJoltVec3(z_up_to_model_space * -steering_axis_z_up); // Direction of the suspension in local space of the body
	w3->mSteeringAxis			= toJoltVec3(z_up_to_model_space *  steering_axis_z_up);
	w3->mWheelUp				= toJoltVec3(z_up_to_model_space *  steering_axis_z_up);
	w3->mWheelForward			= toJoltVec3(z_up_to_model_space * Vec4f(0,1,0,0));
	w3->mWidth = settings.script_settings->rear_wheel_width;
	w3->mSuspensionSpring.mFrequency = settings.script_settings->rear_suspension_spring_freq;
	w3->mSuspensionSpring.mDamping   = settings.script_settings->rear_suspension_spring_damping;
	w3->mMaxSteerAngle = 0.0f;
	w3->mMaxBrakeTorque = max_brake_torque;
	w3->mMaxHandBrakeTorque = max_handbrake_torque;

	JPH::WheelSettingsWV* w4 = new JPH::WheelSettingsWV;
	w4->mPosition = toJoltVec3(back_right_wheel_pos_ms + z_up_to_model_space * Vec4f(0, 0, settings.script_settings->rear_suspension_min_length + settings.script_settings->rear_wheel_attachment_point_raise_dist, 0));
	w4->mSuspensionDirection	= toJoltVec3(z_up_to_model_space * -steering_axis_z_up); // Direction of the suspension in local space of the body
	w4->mSteeringAxis			= toJoltVec3(z_up_to_model_space *  steering_axis_z_up);
	w4->mWheelUp				= toJoltVec3(z_up_to_model_space *  steering_axis_z_up);
	w4->mWheelForward			= toJoltVec3(z_up_to_model_space * Vec4f(0,1,0,0));
	w4->mWidth = settings.script_settings->rear_wheel_width;
	w4->mSuspensionSpring.mFrequency = settings.script_settings->rear_suspension_spring_freq;
	w4->mSuspensionSpring.mDamping   = settings.script_settings->rear_suspension_spring_damping;
	w4->mMaxSteerAngle = 0.0f;
	w4->mMaxBrakeTorque = max_brake_torque;
	w4->mMaxHandBrakeTorque = max_handbrake_torque;

	vehicle.mWheels = { w1, w2, w3, w4 };

	for(size_t i=0; i<4; ++i)
	{
		JPH::WheelSettings* w = vehicle.mWheels[i];
		w->mRadius = (i < 2) ? settings.script_settings->front_wheel_radius : settings.script_settings->rear_wheel_radius;
		
		w->mSuspensionMinLength = (i < 2) ? settings.script_settings->front_suspension_min_length : settings.script_settings->rear_suspension_min_length;
		w->mSuspensionMaxLength = (i < 2) ? settings.script_settings->front_suspension_max_length : settings.script_settings->rear_suspension_max_length;

		const float longitudinal_friction_factor = settings.script_settings->longitudinal_friction_factor;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[0].mY *= longitudinal_friction_factor; // 15;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[1].mY *= longitudinal_friction_factor; // 8;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLongitudinalFriction.mPoints[2].mY *= longitudinal_friction_factor; // 3;

		const float lateral_friction_factor = settings.script_settings->lateral_friction_factor;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[0].mY *= lateral_friction_factor; // 3.;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[1].mY *= lateral_friction_factor; // 3.;
		dynamic_cast<JPH::WheelSettingsWV*>(w)->mLateralFriction.mPoints[2].mY *= lateral_friction_factor; // 3.;
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

	controller->mEngine.mMaxTorque = settings.script_settings->engine_max_torque;
	controller->mEngine.mMaxRPM = settings.script_settings->engine_max_RPM;

	//controller->mTransmission.mMode = JPH::ETransmissionMode::Manual;
	//controller->mTransmission.mGearRatios = JPH::Array<float>(1, 2.9f); // Use a single forwards gear

	// Anti-roll bars
	vehicle.mAntiRollBars.resize(2);
	vehicle.mAntiRollBars[0].mLeftWheel  = 0;
	vehicle.mAntiRollBars[0].mRightWheel = 1;
	vehicle.mAntiRollBars[1].mLeftWheel  = 2;
	vehicle.mAntiRollBars[1].mRightWheel = 3;

	vehicle_constraint = new JPH::VehicleConstraint(*jolt_body, vehicle);
	physics_world.physics_system->AddConstraint(vehicle_constraint);
	physics_world.physics_system->AddStepListener(vehicle_constraint);


	// Set the collision tester
	vehicle_constraint->SetVehicleCollisionTester(m_tester);


	// Get indices of joint nodes
	for(int i=0; i<4; ++i)
	{
		wheel_node_indices[i] = -1;
		wheelbrake_node_indices[i] = -1;
	}

	if(object->opengl_engine_ob)
	{
		wheel_node_indices[0] = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex(settings.script_settings->front_left_wheel_joint_name);
		wheel_node_indices[1] = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex(settings.script_settings->front_right_wheel_joint_name);
		wheel_node_indices[2] = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex(settings.script_settings->back_left_wheel_joint_name);
		wheel_node_indices[3] = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex(settings.script_settings->back_right_wheel_joint_name);

		wheelbrake_node_indices[0] = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex(settings.script_settings->front_left_wheel_brake_joint_name);
		wheelbrake_node_indices[1] = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex(settings.script_settings->front_right_wheel_brake_joint_name);
		wheelbrake_node_indices[2] = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex(settings.script_settings->back_left_wheel_brake_joint_name);
		wheelbrake_node_indices[3] = object->opengl_engine_ob->mesh_data->animation_data.getNodeIndex(settings.script_settings->back_right_wheel_brake_joint_name);
	}
}


CarPhysics::~CarPhysics()
{
	m_physics_world->physics_system->RemoveConstraint(vehicle_constraint);
	m_physics_world->physics_system->RemoveStepListener(vehicle_constraint);
	vehicle_constraint = NULL;

	removeVisualisationObs();
}


void CarPhysics::vehicleSummoned() // Set engine revs to zero etc.
{
	JPH::WheeledVehicleController* controller = static_cast<JPH::WheeledVehicleController*>(vehicle_constraint->GetController());
	controller->GetEngine().SetCurrentRPM(0);
	
	vehicle_constraint->GetWheel(0)->SetAngularVelocity(0);
}


void CarPhysics::startRightingVehicle()
{
	righting_time_remaining = 2;
}


void CarPhysics::userEnteredVehicle(int seat_index) // Should set cur_seat_index
{
	assert(seat_index >= 0 && seat_index < (int)getSettings().seat_settings.size());

	if(seat_index == 0)
		user_in_driver_seat = true;

	righting_time_remaining = -1; // Stop righting vehicle
}


void CarPhysics::userExitedVehicle(int old_seat_index) // Should set cur_seat_index
{
	if(old_seat_index == 0)
		user_in_driver_seat = false;
}


VehiclePhysicsUpdateEvents CarPhysics::update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime)
{
	VehiclePhysicsUpdateEvents events;
	
	assert(this->car_body_id == world_object->physics_object->jolt_body_id);

	// Determine acceleration and brake
	float forward = 0.0f;
	if(physics_input.W_down || physics_input.up_down)
		forward = 1.0f;
	else if(physics_input.S_down || physics_input.down_down)
		forward = -1.0f;

	const float brake = physics_input.space_down ? 1.f : 0.f;
	const float hand_brake = physics_input.B_down ? 1.f : 0.f;

	const float STEERING_SPEED = 3.f;
	if(physics_input.A_down && !physics_input.D_down)
		cur_steering_right = myClamp(cur_steering_right - STEERING_SPEED * (float)dtime, -1.f, 1.f);
	else if(physics_input.D_down && !physics_input.A_down)
		cur_steering_right = myClamp(cur_steering_right + STEERING_SPEED * (float)dtime, -1.f, 1.f);
	else
	{
		if(cur_steering_right > 0)
			cur_steering_right = myMax(cur_steering_right - STEERING_SPEED * (float)dtime, 0.f); // Relax to neutral steering position
		else if(cur_steering_right < 0)
			cur_steering_right = myMin(cur_steering_right + STEERING_SPEED * (float)dtime, 0.f); // Relax to neutral steering position
	}

	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();

	const JPH::Mat44 transform = body_interface.GetWorldTransform(car_body_id);

	JPH::Float4 cols[4];
	transform.StoreFloat4x4(cols);

	const Matrix4f to_world(&cols[0].x);

	// On user input, assure that the car is active
	if(cur_steering_right != 0.0f || forward != 0.0f || brake != 0.0f || hand_brake != 0.0f)
		body_interface.ActivateBody(car_body_id);

	// Pass the input on to the constraint
	JPH::WheeledVehicleController* controller = static_cast<JPH::WheeledVehicleController *>(vehicle_constraint->GetController());
	controller->SetDriverInput(forward, cur_steering_right, brake, hand_brake);


	// model/object space to y-forward/z-up space = R
	const Quatf R_quat = settings.script_settings->model_to_y_forwards_rot_2 * settings.script_settings->model_to_y_forwards_rot_1;

	// Vectors in y-forward/z-up space
	const Vec4f forwards_y_for(0,1,0,0);
	const Vec4f right_y_for(1,0,0,0);
	const Vec4f up_y_for(0,0,1,0);

	const Matrix4f y_forward_to_model_space = (R_quat.conjugate()).toMatrix();
	const Vec4f forwards_os = y_forward_to_model_space * forwards_y_for;
	const Vec4f right_os = y_forward_to_model_space * right_y_for;

	// Apply righting forces to car if righting it:
	if(righting_time_remaining > 0) // If currently righting car:
	{
		// Get current rotation, compute the desired rotation, which is a rotation with the current yaw but no pitch or roll, 
		// compute a rotation to get from current to desired
		const JPH::Quat current_rot = body_interface.GetRotation(car_body_id);

		const Vec4f right_vec_ws   = to_world * right_os;
		const Vec4f forward_vec_ws = to_world * forwards_os;

		const Vec4f up_ws = Vec4f(0,0,1,0);
		const Vec4f no_roll_vehicle_right_ws = normalise(crossProduct(forward_vec_ws, up_ws));
		Vec4f no_roll_vehicle_up_ws = normalise(crossProduct(no_roll_vehicle_right_ws, forward_vec_ws));
		if(dot(no_roll_vehicle_right_ws, right_vec_ws) < 0)
			no_roll_vehicle_up_ws = -no_roll_vehicle_up_ws;

		const float current_yaw_angle = std::atan2(no_roll_vehicle_right_ws[1], no_roll_vehicle_right_ws[0]); // = rotation of right vector around the z vector

		const JPH::Quat desired_rot = JPH::Quat::sRotation(JPH::Vec3(0,0,1), current_yaw_angle) * toJoltQuat(R_quat);

		const JPH::Quat cur_to_desired_rot = desired_rot * current_rot.Conjugated();
		JPH::Vec3 axis;
		float angle;
		cur_to_desired_rot.GetAxisAngle(axis, angle);

		// Choose a desired angular velocity which is proportional in magnitude to how far we need to rotate.
		// Note that we can't just apply a constant torque in the desired rotation direction, or we get angular oscillations around the desired rotation.
		const JPH::Vec3 desired_angular_vel = (axis * angle) * 3;

		// Apply a torque to try and match the desired angular velocity.
		const JPH::Vec3 angular_vel = body_interface.GetAngularVelocity(car_body_id);
		const JPH::Vec3 correction_torque = (desired_angular_vel - angular_vel) * world_object->mass * 2.f;//3.5f;
		body_interface.AddTorque(car_body_id, correction_torque);

		righting_time_remaining -= dtime;
	}



	// Set car joint node transforms
	GLObject* graphics_ob = world_object->opengl_engine_ob.ptr();
	if(graphics_ob)
	{
		const Vec4f steering_axis = normalise(y_forward_to_model_space * up_y_for);

		for(int i=0; i<4; ++i)
		{
			const JPH::Wheel* wheel = vehicle_constraint->GetWheel(i);
			const float sus_len = wheel->GetSuspensionLength() * ob_to_world_scale;

			const Vec4f suspension_dir = -steering_axis;

			const float suspension_min_length = (i < 2) ? settings.script_settings->front_suspension_min_length : settings.script_settings->rear_suspension_min_length;
			const float wheel_attachment_point_raise_dist = (i < 2) ? settings.script_settings->front_wheel_attachment_point_raise_dist : settings.script_settings->rear_wheel_attachment_point_raise_dist;

			// Wheel
			{
				const int node_i = wheel_node_indices[i];

				if(node_i >= 0 && node_i < (int)graphics_ob->anim_node_data.size())
				{
					const Vec4f translation = suspension_dir * (sus_len - (suspension_min_length + wheel_attachment_point_raise_dist));
					graphics_ob->anim_node_data[node_i].procedural_transform = Matrix4f::translationMatrix(translation) * Matrix4f::rotationAroundYAxis(wheel->GetSteerAngle()) * Matrix4f::rotationAroundXAxis(wheel->GetRotationAngle());
				}
			}

			// Wheel brake
			{
				const int node_i = wheelbrake_node_indices[i];
		
				if(node_i >= 0 && node_i < (int)graphics_ob->anim_node_data.size())
				{
					// Just rotating around y axis rotates in place, so we need to translate to wheel space, rotate, then translate back.
					const float wheel_brake_axle_dist = 0.18f;
					const Vec4f translation = suspension_dir * (sus_len - (suspension_min_length + wheel_attachment_point_raise_dist));
					graphics_ob->anim_node_data[node_i].procedural_transform = Matrix4f::translationMatrix(translation) * 
						Matrix4f::translationMatrix(forwards_os * -wheel_brake_axle_dist) * Matrix4f::rotationAroundYAxis(wheel->GetSteerAngle()) * Matrix4f::translationMatrix(forwards_os * wheel_brake_axle_dist);
				}
			}
		}
	}


	// Set volume for tire squeal sounds, do tire-skid smoke/dust particle effects
	for(int i=0; i<4; ++i)
	{
		const JPH::Wheel* wheel = vehicle_constraint->GetWheel(i);
		if(wheel->HasContact())
		{
			// See WheelWV::Update() in Jolt\Physics\Vehicle\WheeledVehicleController.cpp
			JPH::Vec3 relative_velocity = body_interface.GetPointVelocity(car_body_id, wheel->GetContactPosition()) - wheel->GetContactPointVelocity();

			// Cancel relative velocity in the normal plane
			relative_velocity -= wheel->GetContactNormal().Dot(relative_velocity) * wheel->GetContactNormal();

			const float relative_longitudinal_velocity = relative_velocity.Dot(wheel->GetContactLongitudinal());

			const float wheel_radius = (i < 2) ? settings.script_settings->front_wheel_radius : settings.script_settings->rear_wheel_radius;
			const float longitudinal_vel_diff = wheel->GetAngularVelocity() * wheel_radius - relative_longitudinal_velocity;

			const float relative_lateral_vel = relative_velocity.Dot(wheel->GetContactLateral());

			const float sidewards_skid_speed = std::fabs(relative_lateral_vel);
			const float forwards_skid_speed = std::fabs(longitudinal_vel_diff); 
			const float skid_speed = forwards_skid_speed + sidewards_skid_speed;

			// Compute wheel contact position.  Use getBodyTransform and GetWheelLocalTransform instead of GetContactPosition() since that lags behind the actual contact position a bit.
			//const Vec4f contact_point_ws = toVec4fPos(wheel->GetContactPosition()); // getBodyTransform(*m_physics_world) * (wheel_to_local_transform * Vec4f(0, 0, 0, 1) - toVec4fVec(wheel_up_os) * wheel_radius);
			JPH::Vec3 wheel_forward_os, wheel_up_os, wheel_right_os;
			vehicle_constraint->GetWheelLocalBasis(wheel, wheel_forward_os, wheel_up_os, wheel_right_os);
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
		}
	}


	// conPrint("RPM: " + doubleToStringNDecimalPlaces(controller->GetEngine().GetCurrentRPM(), 1));
	// conPrint("engine torque: " + doubleToStringNDecimalPlaces(controller->GetEngine().GetTorque(forward), 1));
	// conPrint("current gear: " + toString(controller->GetTransmission().GetCurrentGear()));

	return events;
}


Vec4f CarPhysics::getFirstPersonCamPos(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const
{
	const Matrix4f seat_to_world = getSeatToWorldTransformNoScale(physics_world, seat_index, use_smoothed_network_transform);
	return seat_to_world * Vec4f(0,0,0.6f,1); // Raise camera position to appox head position
}


Vec4f CarPhysics::getThirdPersonCamTargetTranslation() const
{
	return Vec4f(0, 0, 0, 0);
}


Matrix4f CarPhysics::getBodyTransform(PhysicsWorld& physics_world) const
{
	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();
	const JPH::Mat44 transform = body_interface.GetWorldTransform(car_body_id);

	JPH::Float4 cols[4];
	transform.StoreFloat4x4(cols);

	return Matrix4f(&cols[0].x);
}


Matrix4f CarPhysics::getWheelToWorldTransform(PhysicsWorld& physics_world, int wheel_index) const
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
Matrix4f CarPhysics::getSeatToWorldTransformNoScale(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const
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


Matrix4f CarPhysics::getObjectToWorldTransformNoScale(PhysicsWorld& physics_world, bool use_smoothed_network_transform) const
{
	if(use_smoothed_network_transform && world_object->physics_object)
		return world_object->physics_object->getSmoothedObToWorldNoScaleMatrix();
	else
		return getBodyTransform(physics_world);
}


Vec4f CarPhysics::getLinearVel(PhysicsWorld& physics_world) const
{
	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();
	return toVec4fVec(body_interface.GetLinearVelocity(car_body_id));
}


void CarPhysics::setDebugVisEnabled(bool enabled, OpenGLEngine& opengl_engine)
{
	this->show_debug_vis_obs = enabled;
	this->m_opengl_engine = &opengl_engine;

	if(!enabled)
		removeVisualisationObs();
}


void CarPhysics::updateDopplerEffect(const Vec4f& listener_linear_vel, const Vec4f& listener_pos)
{
}


std::string CarPhysics::getUIInfoMsg()
{
	return std::string();
}


void CarPhysics::updateDebugVisObjects()
{
	if(show_debug_vis_obs)
	{
		const Matrix4f R_inv = ((settings.script_settings->model_to_y_forwards_rot_2 * settings.script_settings->model_to_y_forwards_rot_1).conjugate()).toMatrix();
		const Matrix4f z_up_to_model_space = R_inv;

		//------------------ convex hull points ------------------
		{
			convex_hull_pts_gl_obs.resize(convex_hull_pts.size());
			for(size_t i=0; i<convex_hull_pts_gl_obs.size(); ++i)
			{
				const float radius = 0.06f;
				if(convex_hull_pts_gl_obs[i].isNull())
				{
					convex_hull_pts_gl_obs[i] = m_opengl_engine->makeSphereObject(radius, Colour4f(1,0,0,0.5));
					m_opengl_engine->addObject(convex_hull_pts_gl_obs[i]);
				}

				convex_hull_pts_gl_obs[i]->ob_to_world_matrix = getBodyTransform(*m_physics_world) * Matrix4f::translationMatrix(toVec4fPos(convex_hull_pts[i])) * Matrix4f::uniformScaleMatrix(radius);

				m_opengl_engine->updateObjectTransformData(*convex_hull_pts_gl_obs[i]);
			}
		}

		//------------------ wheels (wheel collision tester cylinder) ------------------
		for(int i=0; i<4; ++i)
		{
			const float radius = (i < 2) ? settings.script_settings->front_wheel_radius : settings.script_settings->rear_wheel_radius;
			if(coll_tester_gl_ob[i].isNull())
			{
				coll_tester_gl_ob[i] = m_opengl_engine->makeCylinderObject(radius, Colour4f(0,0,1,0.5)); // A cylinder from (0,0,0), to (0,0,1) with radius 1;
				m_opengl_engine->addObject(coll_tester_gl_ob[i]);
			}

			const Matrix4f wheel_to_world_transform = toMatrix4f(vehicle_constraint->GetWheelWorldTransform(i, /*inWheelRight=*/JPH::Vec3::sAxisZ(), /*inWheelUp=*/JPH::Vec3::sAxisX()));

			coll_tester_gl_ob[i]->ob_to_world_matrix = 
				wheel_to_world_transform *
				Matrix4f::scaleMatrix(radius, radius, vehicle_constraint->GetWheel(i)->GetSettings()->mWidth) * // scale cylinder
				Matrix4f::translationMatrix(0,0,-0.5f); // centre cylinder around origin

			m_opengl_engine->updateObjectTransformData(*coll_tester_gl_ob[i]);
		}
	}
}


void CarPhysics::removeVisualisationObs()
{
	if(m_opengl_engine)
	{
		for(size_t i=0; i<convex_hull_pts_gl_obs.size(); ++i)
			m_opengl_engine->removeObject(convex_hull_pts_gl_obs[i]);
		convex_hull_pts_gl_obs.clear();

		for(size_t i=0; i<4; ++i)
			checkRemoveObAndSetRefToNull(m_opengl_engine, coll_tester_gl_ob[i]);
	}
}

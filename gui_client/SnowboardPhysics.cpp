/*=====================================================================
SnowboardPhysics.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "SnowboardPhysics.h"


#include "AvatarGraphics.h"
#include "PhysicsWorld.h"
#include "JoltUtils.h"
#include <opengl/OpenGLEngine.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <cmath>


SnowboardPhysics::SnowboardPhysics(WorldObject* object_, Reference<Scripting::SnowboardScriptSettings> settings_, PhysicsWorld& world)
:	object(object_), settings(settings_), physics_world(&world), body_id(object_->physics_object->jolt_body_id)
{
	m_opengl_engine = nullptr;
	show_debug_vis_obs = false;
	occupied = false;
	jump_was_down = false;
	grounded = false;
	jump_cooldown = 0;
	righting_time = 0;
	crouch = 0;
	steering = 0;
	rider_lean = 0;

	ground_normal = Vec4f(0,0,1,0);

	JPH::BodyInterface& bodies = world.physics_system->GetBodyInterface();
	original_shape = bodies.GetShape(body_id);
	original_friction = bodies.GetFriction(body_id);

	const JPH::Quat model_to_board = toJoltQuat(settings->model_to_y_forwards_rot_2 * settings->model_to_y_forwards_rot_1);
	const JPH::Quat board_to_model = model_to_board.Conjugated();
	// Jolt bounds are relative to the centre of mass. Put them back in model
	// space before deriving the board's dimensions and suspension locations.
	const JPH::AABox board_bounds = original_shape->GetLocalBounds().Transformed(
		JPH::Mat44::sRotation(model_to_board) * JPH::Mat44::sTranslation(original_shape->GetCenterOfMass()));
	const JPH::Vec3 centre = board_bounds.GetCenter();
	const JPH::Vec3 half_extent = JPH::Vec3::sMax(board_bounds.GetExtent(), JPH::Vec3::sReplicate(0.01f));
	const float convex_radius = myMin(0.02f, half_extent.ReduceMin() * 0.5f);
	JPH::Ref<JPH::Shape> rounded_box = new JPH::BoxShape(half_extent, convex_radius);
	JPH::Ref<JPH::Shape> crash_shape = new JPH::RotatedTranslatedShape(board_to_model * centre, board_to_model, rounded_box);

	// Keep both hulls centred on the original centre of mass so changing hulls
	// does not shift the suspension's force points or the existing mass/inertia.
	riding_shape = new JPH::OffsetCenterOfMassShape(crash_shape, original_shape->GetCenterOfMass() - crash_shape->GetCenterOfMass());
	const float hull_height_offset = 0.15f;
	JPH::Ref<JPH::Shape> raised_box = new JPH::RotatedTranslatedShape(board_to_model * (centre + JPH::Vec3(0,0,hull_height_offset)), board_to_model, rounded_box);
	raised_riding_shape = new JPH::OffsetCenterOfMassShape(raised_box, original_shape->GetCenterOfMass() - raised_box->GetCenterOfMass());

	JPH::VehicleConstraintSettings vehicle;
	vehicle.mUp = board_to_model * JPH::Vec3(0,0,1);
	vehicle.mForward = board_to_model * JPH::Vec3(0,1,0);
	vehicle.mMaxPitchRollAngle = Maths::pi<float>(); // No invisible upright constraint while airborne.

	const float radius = myClamp(half_extent.GetX() * 0.4f, 0.015f, 0.07f);
	const float clearance = 0.015f; // About 1 cm under load, keeping the hull clear of small terrain seams.
	const float max_suspension_length = 0.16f;
	const float attachment_z = centre.GetZ() - half_extent.GetZ() + max_suspension_length + radius - clearance;
	for(int i=0; i<4; ++i)
	{
		JPH::WheelSettingsWV* wheel = new JPH::WheelSettingsWV();
		wheel->mPosition = board_to_model * JPH::Vec3(
			centre.GetX() + ((i & 1) ? 1.f : -1.f) * half_extent.GetX(),
			centre.GetY() + ((i & 2) ? 1.f : -1.f) * half_extent.GetY() * 0.8f, attachment_z);
		// Support the edges, but apply suspension forces along the centre-of-mass
		// longitudinal axis so the four springs do not fight the carving roll.
		wheel->mEnableSuspensionForcePoint = true;
		wheel->mSuspensionForcePoint = original_shape->GetCenterOfMass() + board_to_model *
			JPH::Vec3(0, ((i & 2) ? 1.f : -1.f) * half_extent.GetY() * 0.8f, 0);
		wheel->mSuspensionDirection = -vehicle.mUp;
		wheel->mSteeringAxis = vehicle.mUp;
		wheel->mWheelUp = vehicle.mUp;
		wheel->mWheelForward = vehicle.mForward;
		wheel->mRadius = radius;
		wheel->mWidth = radius * 2;
		wheel->mSuspensionMinLength = 0.02f;
		wheel->mSuspensionMaxLength = max_suspension_length;
		wheel->mSuspensionSpring.mFrequency = 7.0f;
		wheel->mSuspensionSpring.mDamping = 1.0f;
		wheel->mMaxSteerAngle = 0;
		wheel->mMaxBrakeTorque = 0;
		wheel->mMaxHandBrakeTorque = 0;
		// Riding grip is applied by our controller, not the tire solver.
		for(size_t z=0; z<wheel->mLongitudinalFriction.mPoints.size(); ++z)
			wheel->mLongitudinalFriction.mPoints[z].mY = 0;
		for(size_t z=0; z<wheel->mLateralFriction.mPoints.size(); ++z)
			wheel->mLateralFriction.mPoints[z].mY = 0;
		vehicle.mWheels.push_back(wheel);
	}
	JPH::WheeledVehicleControllerSettings* controller = new JPH::WheeledVehicleControllerSettings();
	controller->mEngine.mMaxTorque = 0;
	controller->mTransmission.mMode = JPH::ETransmissionMode::Manual; // Stay in neutral; retain the required positive clutch strength.
	// Jolt requires connected differential torque ratios to sum to one, even
	// with no engine torque. Neutral gear and a disengaged clutch keep the probes uncoupled.
	controller->mDifferentials.resize(1);
	controller->mDifferentials[0].mLeftWheel = 0;
	controller->mDifferentials[0].mRightWheel = 1;
	controller->mDifferentials[0].mEngineTorqueRatio = 1.f;
	vehicle.mController = controller;

	// Keep the existing mass and inertia. Only replace the collision hull; the
	// visual model, model-space origin and rider bindings are unchanged.
	bodies.SetShape(body_id, riding_shape, /*inUpdateMassProperties=*/false, JPH::EActivation::Activate);
	bodies.SetFriction(body_id, 0.8f); // Parked hull friction, including when tipped over.
	original_enhanced_internal_edge_removal = false;
	original_object_layer = bodies.GetObjectLayer(body_id);
	{
		JPH::BodyLockWrite lock(world.physics_system->GetBodyLockInterface(), body_id);
		assert(lock.Succeeded());
		JPH::Body& body = lock.GetBody();
		original_enhanced_internal_edge_removal = body.GetEnhancedInternalEdgeRemoval();
		body.SetEnhancedInternalEdgeRemoval(true);
		vehicle_constraint = new JPH::VehicleConstraint(body, vehicle);
	}
	static_cast<JPH::WheeledVehicleController*>(vehicle_constraint->GetController())->GetTransmission().Set(0, 0.f);
	vehicle_constraint->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterCastSphere(Layers::MOVING, radius, JPH::Vec3(0,0,1), degreeToRad(65.f)));
	world.physics_system->AddConstraint(vehicle_constraint);
	world.physics_system->AddStepListener(vehicle_constraint);
	suspension_enabled = true;
	setSuspensionEnabled(false); // Unoccupied boards start as ordinary rigid bodies.
}


SnowboardPhysics::~SnowboardPhysics()
{
	removeVisualisationObs();
	setSuspensionEnabled(false);
	vehicle_constraint = nullptr;
}


void SnowboardPhysics::setSuspensionEnabled(bool enabled)
{
	if(enabled == suspension_enabled)
		return;

	if(suspension_enabled)
	{
		physics_world->physics_system->RemoveStepListener(vehicle_constraint);
		physics_world->physics_system->RemoveConstraint(vehicle_constraint);
	}

	// The body may already have been removed during object/script reload.
	bool body_exists = false;
	JPH::RefConst<JPH::Shape> target_shape = enabled ? riding_shape : original_shape;
	{
		JPH::BodyLockWrite lock(physics_world->physics_system->GetBodyLockInterface(), body_id);
		if(lock.Succeeded())
		{
			body_exists = true;
			JPH::Body& body = lock.GetBody();
			body.SetFriction(enabled ? 0.02f : original_friction);
			body.SetEnhancedInternalEdgeRemoval(enabled ? true : original_enhanced_internal_edge_removal);
			const JPH::Quat board_to_model = toJoltQuat(settings->model_to_y_forwards_rot_2 * settings->model_to_y_forwards_rot_1).Conjugated();
			if(enabled && (body.GetRotation() * board_to_model * JPH::Vec3(0,0,1)).GetZ() > 0.5f)
				target_shape = raised_riding_shape;
		}
	}
	suspension_enabled = enabled && body_exists;
	if(body_exists)
	{
		JPH::BodyInterface& bodies = physics_world->physics_system->GetBodyInterface();
		bodies.SetObjectLayer(body_id, original_object_layer);
		bodies.SetShape(body_id, target_shape, /*inUpdateMassProperties=*/false, JPH::EActivation::Activate);
		bodies.InvalidateContactCache(body_id);
	}
	if(suspension_enabled)
	{
		// Recreate the constraint so contact IDs and warm-start impulses from the
		// previous ride cannot be reused after the object has moved or terrain changed.
		const JPH::Ref<JPH::ConstraintSettings> constraint_settings = vehicle_constraint->GetConstraintSettings();
		const auto* vehicle_settings = static_cast<const JPH::VehicleConstraintSettings*>(constraint_settings.GetPtr());
		const JPH::RefConst<JPH::VehicleCollisionTester> collision_tester = vehicle_constraint->GetVehicleCollisionTester();
		{
			JPH::BodyLockWrite lock(physics_world->physics_system->GetBodyLockInterface(), body_id);
			vehicle_constraint = new JPH::VehicleConstraint(lock.GetBody(), *vehicle_settings);
		}
		static_cast<JPH::WheeledVehicleController*>(vehicle_constraint->GetController())->GetTransmission().Set(0, 0.f);
		vehicle_constraint->SetVehicleCollisionTester(collision_tester);
		physics_world->physics_system->AddConstraint(vehicle_constraint);
		physics_world->physics_system->AddStepListener(vehicle_constraint);
	}
	grounded = false;
	jump_cooldown = 0;
	steering = 0;
	rider_lean = 0;
}


void SnowboardPhysics::userEnteredVehicle(int seat_index)
{
	assert(seat_index == 0);
	occupied = true;
	setSuspensionEnabled(true);
	jump_was_down = false;
}


void SnowboardPhysics::userExitedVehicle(int old_seat_index)
{
	occupied = false;
	setSuspensionEnabled(false);
	righting_time = 0;
	jump_was_down = false;
	last_physics_input_bitflags = 0;
}


VehiclePhysicsUpdateEvents SnowboardPhysics::update(PhysicsWorld& world, const PlayerPhysicsInput& input, float dt)
{
	if(dt <= 0 || !isFinite(dt) || (!occupied && righting_time <= 0))
		return VehiclePhysicsUpdateEvents();
	JPH::BodyInterface& bodies = world.physics_system->GetBodyInterface();
	const Matrix4f board_to_world = getBodyTransform(world) * object->vehicle_script->getZUpToModelSpaceTransform();
	const Vec4f up = board_to_world * Vec4f(0,0,1,0);
	const Vec4f forward = board_to_world * Vec4f(0,1,0,0);
	const Vec4f right = board_to_world * Vec4f(1,0,0,0);

	// Use the unraised hull before the suspension faces sideways or upwards.
	// Hysteresis avoids switching repeatedly near the tipping threshold.
	if(suspension_enabled)
	{
		const JPH::RefConst<JPH::Shape> current_shape = bodies.GetShape(body_id);
		const bool use_raised_hull = up[2] > (current_shape.GetPtr() == raised_riding_shape.GetPtr() ? 0.35f : 0.5f);
		const JPH::RefConst<JPH::Shape> target_shape = use_raised_hull ? raised_riding_shape : riding_shape;
		if(current_shape.GetPtr() != target_shape.GetPtr())
			bodies.SetShape(body_id, target_shape, /*inUpdateMassProperties=*/false, JPH::EActivation::Activate);
	}
	const Vec4f vel = getLinearVel(world);
	const float speed = vel.length();
	const float mass = myMax(0.1f, object->mass);
	// Contacts and suspension impulses come from the previous physics substep,
	// as with the other vehicle controllers. Use actual loaded supports rather
	// than a centre ray that can miss the ground at a seam or crest.
	Vec4f normal_sum(0,0,0,0);
	Vec4f ground_velocity(0,0,0,0);
	float support_impulse = 0;
	float support_weights[4] = {0,0,0,0};
	bool dynamic_support[4] = {false,false,false,false};
	for(int i=0; i<4; ++i)
	{
		const JPH::Wheel* wheel = vehicle_constraint->GetWheel(i);
		if(suspension_enabled && wheel->HasContact() && wheel->GetSuspensionLambda() > 0)
		{
			JPH::BodyLockRead lock(world.physics_system->GetBodyLockInterface(), wheel->GetContactBodyID());
			if(!lock.Succeeded() || !lock.GetBody().IsInBroadPhase())
				continue; // A terrain LOD replacement may have removed last step's support.
			const Vec4f normal = toVec4fVec(wheel->GetContactNormal());
			if(normal[2] > 0.2f && dot(up, normal) > 0.3f)
			{
				const float weight = wheel->GetSuspensionLambda();
				normal_sum += normal * weight;
				ground_velocity += toVec4fVec(lock.GetBody().GetPointVelocity(wheel->GetContactPosition())) * weight;
				support_impulse += weight;
				support_weights[i] = weight;
				dynamic_support[i] = lock.GetBody().IsDynamic();
			}
		}
	}
	const bool was_grounded = grounded;
	grounded = support_impulse > 1.e-5f && jump_cooldown <= 0;
	if(grounded)
	{
		ground_velocity = ground_velocity / support_impulse;
		const Vec4f normal = normalise(normal_sum);
		ground_normal = was_grounded ? normalise(ground_normal + (normal - ground_normal) * (1.f - std::exp(-12.f * dt))) : normal;
	}
	const Vec4f relative_vel = vel - (grounded ? ground_velocity : Vec4f(0,0,0,0));

	float target_lean = 0;
	if(occupied && grounded)
	{
		const Vec4f tangent_forward = normalise(forward - ground_normal * dot(forward, ground_normal));
		const Vec4f tangent_right = normalise(crossProduct(tangent_forward, ground_normal));
		const float forward_speed = dot(relative_vel, tangent_forward);
		const float yaw_rate = dot(toVec4fVec(bodies.GetAngularVelocity(body_id)), ground_normal);
		// tan(lean) = v^2 / (r * g). With signed r = v / yaw_rate, use v * yaw_rate
		// directly, avoiding division by a near-zero turn rate on straight runs.
		const float lateral_accel = -forward_speed * yaw_rate; // Positive towards the board's right edge.
		const float normal_gravity = myMax(1.f, -dot(toVec4fVec(world.physics_system->GetGravity()), ground_normal));
		const float board_lean = std::atan2(dot(up, tangent_right), dot(up, ground_normal));
		target_lean = myClamp(std::atan2(lateral_accel, normal_gravity) - board_lean,
			-degreeToRad(45.f), degreeToRad(45.f)) * myClamp(std::fabs(forward_speed), 0.f, 1.f);
	}
	rider_lean += (target_lean - rider_lean) * (1.f - std::exp(-6.f * dt));

	float steer_input = occupied ? input.axis_left_x : 0;
	if(occupied && (input.A_down || input.left_down))
		steer_input = -1;
	if(occupied && (input.D_down || input.right_down))
		steer_input = 1;
	steer_input = myClamp(steer_input, -1.0f, 1.0f);
	float push = occupied ? myClamp(-input.axis_left_y, 0.0f, 1.0f) : 0;
	if(occupied && (input.W_down || input.up_down))
		push = 1;
	const bool braking = occupied && (input.S_down || input.down_down || input.B_down || input.axis_left_y > 0.2f);
	if(braking)
		push = 0;
	const bool jump = occupied && input.space_down;
	jump_cooldown = myMax(0.0f, jump_cooldown - dt);

	const float blend = 1.0f - std::exp(-8.0f * dt);
	steering += (steer_input - steering) * blend;
	const float target_crouch = (occupied && input.C_down) ? 1.0f : myMax(grounded ? 0.0f : 0.65f, myClamp(speed / settings->fast_speed, 0.0f, 1.0f));
	crouch += (target_crouch - crouch) * blend;

	if(occupied || righting_time > 0)
	{
		bodies.ActivateBody(body_id);
		const Vec4f normal = grounded ? ground_normal : Vec4f(0,0,1,0);
		if(grounded)
		{
			const Vec4f tangent_forward = normalise(forward - normal * dot(forward, normal));
			const Vec4f tangent_right = normalise(crossProduct(tangent_forward, normal));
			const Vec4f tangent_vel = relative_vel - normal * dot(relative_vel, normal);
			// Edge grip suppresses sideways slip. Bound the combined resistance so
			// braking, grip and drag cannot reverse the velocity on a long frame.
			const float edge = myClamp(std::fabs(dot(up, tangent_right)) / std::sin(degreeToRad(35.f)), 0.f, 1.f);
			const float load_accel = myMin(20.f, support_impulse / (dt * mass));
			const float lateral_accel = myClamp(dot(relative_vel, tangent_right) * (0.6f + 4.4f * edge),
				-load_accel * (0.12f + 1.1f * edge), load_accel * (0.12f + 1.1f * edge));
			Vec4f resistance = tangent_right * lateral_accel + tangent_vel * (0.015f * tangent_vel.length());
			if(braking && tangent_vel.length() > 0.001f)
				resistance += normalise(tangent_vel) * myMin(settings->brake_acceleration, load_accel * 1.1f);
			const float max_resistance = tangent_vel.length() / dt;
			if(resistance.length() > max_resistance)
				resistance = normalise(resistance) * max_resistance;
			const Vec4f force = (tangent_forward * (push * settings->push_acceleration) - resistance) * mass;
			bodies.AddForce(body_id, toJoltVec3(force));
			// Match the suspension solver's two-way interaction with moving platforms.
			for(int i=0; i<4; ++i)
			{
				if(dynamic_support[i])
				{
					const JPH::Wheel* wheel = vehicle_constraint->GetWheel(i);
					bodies.AddForce(wheel->GetContactBodyID(), toJoltVec3(-force * (support_weights[i] / support_impulse)), wheel->GetContactPosition());
				}
			}
			if(jump && !jump_was_down && jump_cooldown == 0)
			{
				bodies.AddImpulse(body_id, toJoltVec3(normal * (mass * settings->jump_speed)));
				jump_cooldown = 0.3f;
				grounded = false;
			}
		}


		// Bank onto the inside edge: tan(bank) = v^2 / (r * g) = v * yaw_rate / g.
		// Use the requested turn rate to initiate the carve before yaw has built up.
		const float yaw = -steering * settings->turn_rate * (grounded ? myClamp(speed / 3.0f, 0.0f, 1.0f) : 0.6f);
		Vec4f desired_up = normal;
		if(grounded && righting_time <= 0)
		{
			const Vec4f tangent_forward = normalise(forward - normal * dot(forward, normal));
			const Vec4f tangent_right = normalise(crossProduct(tangent_forward, normal));
			const float normal_gravity = myMax(1.f, -dot(toVec4fVec(world.physics_system->GetGravity()), normal));
			const float bank = myClamp(std::atan2(-dot(relative_vel, tangent_forward) * yaw, normal_gravity),
				-degreeToRad(35.f), degreeToRad(35.f));
			desired_up = normal * std::cos(bank) + tangent_right * std::sin(bank);
		}
		Vec4f correction = (grounded || righting_time > 0) ? crossProduct(up, desired_up) * 8.0f : Vec4f(0,0,0,0);
		if(righting_time > 0 && dot(up, desired_up) < -0.8f)
			correction = right * 6.0f; // Cross product alone is zero when exactly upside down.
		const Vec4f angular_vel = toVec4fVec(bodies.GetAngularVelocity(body_id));
		// Account for the board's small moment of inertia about its long axis.
		// Using mass alone here makes thin boards oscillate violently in roll.
		JPH::Mat44 inertia;
		if(inertia.SetInversed3x3(bodies.GetInverseInertia(body_id)))
		{
			// In the air, preserve pitch and roll momentum. Only assist yaw while
			// steering; releasing the controls leaves the board in free rigid-body motion.
			const Vec4f angular_error = (grounded || righting_time > 0) ? correction + normal * yaw - angular_vel :
				(steer_input != 0 ? normal * (yaw - dot(angular_vel, normal)) : Vec4f(0,0,0,0));
			const Vec4f angular_accel = angular_error * myMin(8.0f, 1.0f / dt);
			bodies.AddTorque(body_id, inertia.Multiply3x3(toJoltVec3(angular_accel)));
		}
		if(righting_time > 1.65f)
			bodies.AddForce(body_id, JPH::Vec3(0,0,mass * 12.0f)); // Give a flipped board clearance to rotate.
	}
	righting_time = myMax(0.0f, righting_time - dt);
	jump_was_down = jump;
	return VehiclePhysicsUpdateEvents();
}


Matrix4f SnowboardPhysics::getBodyTransform(PhysicsWorld& world) const
{
	JPH::Float4 cols[4];
	world.physics_system->GetBodyInterface().GetWorldTransform(body_id).StoreFloat4x4(cols);
	return Matrix4f(&cols[0].x);
}


Matrix4f SnowboardPhysics::getObjectToWorldTransformNoScale(PhysicsWorld& world, bool smoothed) const
{
	return smoothed && object->physics_object ? object->physics_object->getSmoothedObToWorldNoScaleMatrix() : getBodyTransform(world);
}


Matrix4f SnowboardPhysics::getSeatToWorldTransformNoScale(PhysicsWorld& world, uint32 seat_index, bool smoothed) const
{
	assert(seat_index == 0);
	// Unlike a chair seat, this origin is on the deck. AvatarGraphics fits the legs to it.
	return getObjectToWorldTransformNoScale(world, smoothed) * Matrix4f::translationMatrix(settings->seat_settings[0].seat_position) *
		object->vehicle_script->getZUpToModelSpaceTransform() * Matrix4f::rotationAroundZAxis(Maths::pi_2<float>());
}


Vec4f SnowboardPhysics::getFirstPersonCamPos(PhysicsWorld& world, uint32 seat_index, bool smoothed) const
{
	return getSeatToWorldTransformNoScale(world, seat_index, smoothed) * Vec4f(0,0,1.55f - crouch * 0.25f,1);
}


Vec4f SnowboardPhysics::getLinearVel(PhysicsWorld& world) const
{
	return toVec4fVec(world.physics_system->GetBodyInterface().GetLinearVelocity(body_id));
}


void SnowboardPhysics::updateRiderPose(PoseConstraint& pose) const
{
	// Use the supplied (possibly smoothed) seat transform for both the avatar and
	// model-space targets, so the feet do not lag behind the rendered bindings.
	const Scripting::SeatSettings& seat = settings->seat_settings[0];
	const Matrix4f seat_to_model = Matrix4f::translationMatrix(seat.seat_position) *
		object->vehicle_script->getZUpToModelSpaceTransform() * Matrix4f::rotationAroundZAxis(Maths::pi_2<float>());
	Matrix4f model_to_seat;
	seat_to_model.getInverseForAffine3Matrix(model_to_seat);
	const Matrix4f model_to_world = pose.seat_to_world * model_to_seat;
	pose.left_foot_point_ws = isFinite(seat.left_foot_point_os[0]) ? model_to_world * seat.left_foot_point_os : Vec4f(std::numeric_limits<float>::quiet_NaN());
	pose.right_foot_point_ws = isFinite(seat.right_foot_point_os[0]) ? model_to_world * seat.right_foot_point_os : Vec4f(std::numeric_limits<float>::quiet_NaN());
	pose.snowboarding = true;
	pose.snowboard_crouch = crouch;
	pose.snowboard_steer = steering;
	pose.snowboard_lean = rider_lean;
	pose.upper_body_rot_angle = -0.08f - 0.22f * crouch;
	pose.upper_leg_rot_angle = 0.3f + 0.5f * crouch;
	pose.lower_leg_rot_angle = -2.0f * pose.upper_leg_rot_angle;
	pose.upper_leg_rot_around_thigh_bone_angle = 0;
	pose.upper_leg_apart_angle = 0.15f;
	pose.lower_leg_apart_angle = 0;
	pose.rotate_foot_out_angle = 0;
	pose.arm_down_angle = 2.7f - 0.8f * crouch;
	pose.arm_out_angle = 1.2f;
	pose.upper_arm_shoulder_lift_angle = 0;
	pose.lower_arm_up_angle = 0.25f + 0.3f * crouch;
	pose.left_hand_hold_point_ws = pose.right_hand_hold_point_ws = Vec4f(std::numeric_limits<float>::quiet_NaN());
}


void SnowboardPhysics::setDebugVisEnabled(bool enabled, OpenGLEngine& opengl_engine)
{
	show_debug_vis_obs = enabled;
	m_opengl_engine = &opengl_engine;
	if(!enabled)
		removeVisualisationObs();
}


void SnowboardPhysics::updateDebugVisObjects()
{
	if(!show_debug_vis_obs)
		return;

	const Matrix4f ob_to_world = getObjectToWorldTransformNoScale(*physics_world, true);

	// Display the active hull's object-space AABB, rotated with the snowboard.
	{
		JPH::BodyLockRead lock(physics_world->physics_system->GetBodyLockInterface(), body_id);
		if(lock.Succeeded())
		{
			if(!collision_aabb_gl_ob)
			{
				collision_aabb_gl_ob = m_opengl_engine->makeCuboidEdgeAABBObject(Vec4f(0,0,0,1), Vec4f(1,1,1,1), Colour4f(1,0.5f,0,1), 0.02f);
				m_opengl_engine->addObject(collision_aabb_gl_ob);
			}
			const JPH::Shape* shape = lock.GetBody().GetShape();
			const JPH::AABox bounds = shape->GetLocalBounds();
			const JPH::Vec3 min_os = bounds.mMin + shape->GetCenterOfMass(); // Jolt local bounds are relative to the centre of mass.
			const JPH::Vec3 size = bounds.mMax - bounds.mMin;
			collision_aabb_gl_ob->ob_to_world_matrix = ob_to_world * Matrix4f::translationMatrix(toVec4fPos(min_os)) * Matrix4f::scaleMatrix(size.GetX(), size.GetY(), size.GetZ());
			m_opengl_engine->updateObjectTransformData(*collision_aabb_gl_ob);
		}
		else
			checkRemoveObAndSetRefToNull(*m_opengl_engine, collision_aabb_gl_ob);
	}

	const Scripting::SeatSettings& seat = settings->seat_settings[0];
	const Vec4f pos_os[2] = { seat.left_foot_point_os, seat.right_foot_point_os };
	for(size_t i=0; i<2; ++i)
	{
		if(isFinite(pos_os[i][0]))
		{
			const float radius = 0.04f;
			if(!foot_point_gl_obs[i])
			{
				foot_point_gl_obs[i] = m_opengl_engine->makeSphereObject(radius, Colour4f(0,1,1,1));
				m_opengl_engine->addObject(foot_point_gl_obs[i]);
			}
			const Vec4f pos_ws = ob_to_world * pos_os[i];
			foot_point_gl_obs[i]->ob_to_world_matrix = Matrix4f::translationMatrix(pos_ws) * Matrix4f::uniformScaleMatrix(radius);
			m_opengl_engine->updateObjectTransformData(*foot_point_gl_obs[i]);
		}
		else
			checkRemoveObAndSetRefToNull(*m_opengl_engine, foot_point_gl_obs[i]);
	}
}


void SnowboardPhysics::removeVisualisationObs()
{
	if(m_opengl_engine)
	{
		for(size_t i=0; i<2; ++i)
			checkRemoveObAndSetRefToNull(*m_opengl_engine, foot_point_gl_obs[i]);
		checkRemoveObAndSetRefToNull(*m_opengl_engine, collision_aabb_gl_ob);
	}
}


std::string SnowboardPhysics::getUIInfoMsg()
{
	return "W / Up: push, S / Down / B: brake, A / D: steer, C: crouch, Space: jump";
}


#if BUILD_TESTS
#include <utils/StackAllocator.h>
#include <utils/TaskManager.h>
#include <utils/TestUtils.h>


void SnowboardPhysics::test()
{
	// Exercise real suspension contacts on two separate terrain bodies, including
	// a small height mismatch. No terrain-edge rejection is enabled for the board.
	for(int seam_axis=0; seam_axis<2; ++seam_axis)
	for(int direction=-1; direction<=1; direction+=2)
	{
		glare::TaskManager task_manager(1);
		glare::StackAllocator stack_allocator(32 * 1024 * 1024);
		PhysicsWorld world(&task_manager, &stack_allocator);
		// Check every ordered layer pair, including symmetry and non-collidable layers.
		const bool expected_collisions[Layers::NUM_LAYERS][Layers::NUM_LAYERS] = {
			{false, true,  false, false},
			{true,  true,  false, false},
			{false, false, false, false},
			{false, false, false, false}
		};
		for(int a=0; a<Layers::NUM_LAYERS; ++a)
			for(int b=0; b<Layers::NUM_LAYERS; ++b)
				testAssert(world.physics_system->GetDefaultLayerFilter((JPH::ObjectLayer)a).ShouldCollide((JPH::ObjectLayer)b) == expected_collisions[a][b]);
		Array2D<float> heights(8, 8);
		heights.setAllElems(0.f);
		const PhysicsShape terrain_shape = PhysicsWorld::createJoltHeightFieldShape(8, heights, 1.f);
		PhysicsObjectRef terrain[2];
		for(int i=0; i<2; ++i)
		{
			terrain[i] = new PhysicsObject(true);
			terrain[i]->shape = terrain_shape;
			terrain[i]->pos = seam_axis == 0 ? Vec4f(-7.f + i*7.f,-3.5f,i*0.005f,1) : Vec4f(-3.5f,-7.f + i*7.f,i*0.005f,1);
			terrain[i]->rot = Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>());
			terrain[i]->scale = Vec3f(1.f);
			terrain[i]->restitution = 0;
			world.addObject(terrain[i]);
			testAssert(world.physics_system->GetBodyInterface().GetObjectLayer(terrain[i]->jolt_body_id) == Layers::NON_MOVING);
		}

		WorldObjectRef object = new WorldObject();
		object->mass = 80.f;
		Reference<Scripting::SnowboardScriptSettings> settings = new Scripting::SnowboardScriptSettings();
		settings->model_to_y_forwards_rot_1 = Quatf::identity();
		settings->model_to_y_forwards_rot_2 = Quatf::identity();
		settings->seat_settings.push_back(Scripting::SeatSettings());
		object->vehicle_script = new Scripting::SnowboardScript();
		object->vehicle_script->settings = settings;
		object->physics_object = new PhysicsObject(true);
		PhysicsObject& physics_ob = *object->physics_object;
		physics_ob.shape.jolt_shape = new JPH::BoxShape(JPH::Vec3(0.15f,0.8f,0.04f), 0.01f);
		physics_ob.motion_type = PhysicsObject::MotionType_dynamic;
		physics_ob.mass = object->mass;
		physics_ob.restitution = 0;
		physics_ob.scale = Vec3f(1.f);
		const Vec4f travel_dir = seam_axis == 0 ? Vec4f((float)direction,0,0,0) : Vec4f(0,(float)direction,0,0);
		physics_ob.pos = Vec4f(0,0,0.1f,1) - travel_dir * 2.f;
		physics_ob.rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), std::atan2(-travel_dir[0], travel_dir[1]));
		world.addObject(object->physics_object);
		JPH::BodyInterface& bodies = world.physics_system->GetBodyInterface();
		const JPH::RefConst<JPH::Shape> old_shape = bodies.GetShape(physics_ob.jolt_body_id);
		const float old_friction = bodies.GetFriction(physics_ob.jolt_body_id);
		{
			SnowboardPhysics controller(object.ptr(), settings, world);
			controller.userEnteredVehicle(0);
			testAssert(bodies.GetObjectLayer(physics_ob.jolt_body_id) == Layers::MOVING);
			testAssert(bodies.GetShape(physics_ob.jolt_body_id).GetPtr() == controller.raised_riding_shape.GetPtr());
			PlayerPhysicsInput input;
			const float dt = 1.f / 120.f;
			for(int i=0; i<120; ++i)
			{
				controller.update(world, input, dt);
				world.think(dt);
			}
			bodies.SetLinearVelocity(physics_ob.jolt_body_id, toJoltVec3(travel_dir * 3.f));
			for(int i=0; i<240; ++i)
			{
				controller.update(world, input, dt);
				world.think(dt);
				testAssert(controller.getBodyTransform(world).getColumn(3)[2] > 0.045f); // Underside stays clear of the terrain.
			}
			testAssert(dot(controller.getBodyTransform(world).getColumn(3), travel_dir) > 1.f);
			testAssert(dot(controller.getLinearVel(world), travel_dir) > 1.5f);

			input.S_down = true;
			for(int i=0; i<120; ++i)
			{
				controller.update(world, input, dt);
				world.think(dt);
			}
			testAssert(controller.getLinearVel(world).length() < 0.2f);

			controller.userExitedVehicle(0);
			testAssert(!controller.suspension_enabled);
			testAssert(bodies.GetShape(physics_ob.jolt_body_id).GetPtr() == old_shape.GetPtr());
			testAssert(bodies.GetFriction(physics_ob.jolt_body_id) == old_friction);
			testAssert(bodies.GetObjectLayer(physics_ob.jolt_body_id) == Layers::MOVING);
			input.clear();
			bodies.SetLinearVelocity(physics_ob.jolt_body_id, toJoltVec3(travel_dir * 2.f));
			for(int i=0; i<120; ++i)
			{
				controller.update(world, input, dt);
				world.think(dt);
			}
			testAssert(controller.getLinearVel(world).length() < 0.1f);
			const Vec4f parked_pos = controller.getBodyTransform(world).getColumn(3);
			for(int i=0; i<120; ++i)
			{
				controller.update(world, input, dt);
				world.think(dt);
			}
			testAssert((controller.getBodyTransform(world).getColumn(3) - parked_pos).length() < 0.02f);
			controller.userEnteredVehicle(0);
			testAssert(controller.suspension_enabled);
			input.clear();
			for(int i=0; i<120; ++i)
			{
				controller.update(world, input, dt);
				world.think(dt);
			}
			input.space_down = true;
			controller.update(world, input, dt);
			testAssert(controller.getLinearVel(world)[2] > 3.f);
			world.think(dt);
			input.W_down = true;
			const float forward_speed = dot(controller.getLinearVel(world), travel_dir);
			for(int i=0; i<12; ++i)
			{
				controller.update(world, input, dt);
				world.think(dt);
			}
			testAssert(dot(controller.getLinearVel(world), travel_dir) < forward_speed + 0.05f); // No airborne push.
			testAssert(controller.getLinearVel(world)[2] < 4.1f); // Holding Space does not keep adding impulses.

			// An inverted landing must use the hull, then return to suspension when righted.
			input.clear();
			bodies.SetPositionAndRotation(physics_ob.jolt_body_id, JPH::RVec3(0,0,1),
				JPH::Quat::sRotation(JPH::Vec3::sAxisX(), Maths::pi<float>()), JPH::EActivation::Activate);
			bodies.SetLinearAndAngularVelocity(physics_ob.jolt_body_id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
			for(int i=0; i<240; ++i)
			{
				controller.update(world, input, dt);
				testAssert(bodies.GetObjectLayer(physics_ob.jolt_body_id) == Layers::MOVING);
				world.think(dt);
				testAssert(controller.getBodyTransform(world).getColumn(3)[2] > 0.f);
			}
			bodies.SetPositionAndRotation(physics_ob.jolt_body_id, JPH::RVec3(0,0,0.1f),
				JPH::Quat::sIdentity(), JPH::EActivation::Activate);
			controller.update(world, input, dt);
			testAssert(bodies.GetObjectLayer(physics_ob.jolt_body_id) == Layers::MOVING);
			testAssert(bodies.GetShape(physics_ob.jolt_body_id).GetPtr() == controller.raised_riding_shape.GetPtr());

			// Both turn directions should bank onto the inside edge, rather than stay flat.
			for(int turn=-1; turn<=1; turn+=2)
			{
				input.clear();
				bodies.SetPositionAndRotation(physics_ob.jolt_body_id, JPH::RVec3(0,0,0.1f),
					JPH::Quat::sIdentity(), JPH::EActivation::Activate);
				bodies.SetLinearAndAngularVelocity(physics_ob.jolt_body_id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
				for(int i=0; i<120; ++i)
				{
					controller.update(world, input, dt);
					world.think(dt);
				}
				bodies.SetLinearVelocity(physics_ob.jolt_body_id, JPH::Vec3(0,4,0));
				input.axis_left_x = (float)turn;
				for(int i=0; i<90; ++i)
				{
					controller.update(world, input, dt);
					world.think(dt);
				}
				const Matrix4f transform = controller.getBodyTransform(world);
				const Vec4f turn_right = normalise(crossProduct(transform.getColumn(1), Vec4f(0,0,1,0)));
				testAssert(dot(transform.getColumn(2), turn_right) * turn > 0.1f);
				testAssert(transform.getColumn(3)[2] > 0.f);
			}
		}
		testAssert(bodies.GetShape(physics_ob.jolt_body_id).GetPtr() == old_shape.GetPtr());
		testAssert(bodies.GetFriction(physics_ob.jolt_body_id) == old_friction);
		testAssert(bodies.GetObjectLayer(physics_ob.jolt_body_id) == Layers::MOVING);
		world.removeObject(object->physics_object);
		for(int i=0; i<2; ++i)
			world.removeObject(terrain[i]);
	}
}
#endif

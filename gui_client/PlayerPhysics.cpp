/*=====================================================================
PlayerPhysics.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
File created by ClassTemplate on Mon Sep 23 15:14:04 2002
=====================================================================*/
#include "PlayerPhysics.h"


#include "CameraController.h"
#include "PhysicsWorld.h"
#include "PhysicsObject.h"
#include <StringUtils.h>
#include <ConPrint.h>
#include <PlatformUtils.h>


#if USE_JOLT_PLAYER_PHYSICS
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Character/Character.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#endif


static const float runfactor = 5; // How much faster you move when the run button (shift) is held down.
static const float movespeed = 3;
static const float jumpspeed = 4.5;
static const float maxairspeed = 8;


static const float JUMP_PERIOD = 0.1f; // Allow a jump command to be executed even if the player is not quite on the ground yet.

const float SPHERE_RAD = 0.3f;
const float EYE_HEIGHT = 1.67f;

PlayerPhysics::PlayerPhysics()
:	vel(0,0,0),
	moveimpulse(0,0,0),
	lastgroundnormal(0,0,1),
	lastvel(0,0,0),
	jumptimeremaining(0),
	onground(false),
	flymode(false),
	last_runpressed(false),
	time_since_on_ground(0),
	campos_z_delta(0)
{
}


PlayerPhysics::~PlayerPhysics()
{
}


#if USE_JOLT_PLAYER_PHYSICS
inline static JPH::Vec3 toJoltVec3(const Vec4f& v)
{
	return JPH::Vec3(v[0], v[1], v[2]);
}

inline static JPH::Vec3 toJoltVec3(const Vec3d& v)
{
	return JPH::Vec3((float)v.x, (float)v.y, (float)v.z);
}

inline static Vec3f toVec3f(const JPH::Vec3& v)
{
	return Vec3f(v.GetX(), v.GetY(), v.GetZ());
}
inline static Vec4f toVec4fVec(const JPH::Vec3& v)
{
	return Vec4f(v.GetX(), v.GetY(), v.GetZ(), 0.f);
}
#endif


void PlayerPhysics::init(PhysicsWorld& physics_world, const Vec3d& initial_player_pos)
{
#if USE_JOLT_PLAYER_PHYSICS
	const float	cCharacterHeightStanding = 1.25f; // Chosen so the capsule top is about the same height as the head of xbot.glb.  Can test this by jumping into an overhead ledge :)
	const float	cCharacterRadiusStanding = SPHERE_RAD;

	JPH::RefConst<JPH::Shape> mStandingShape = JPH::RotatedTranslatedShapeSettings(
		JPH::Vec3(0, 0, 0.5f * cCharacterHeightStanding + cCharacterRadiusStanding), // position
		JPH::Quat::sRotation(JPH::Vec3(1, 0, 0), Maths::pi_2<float>()), // rotate capsule from extending in the y-axis to the z-axis.
		new JPH::CapsuleShape(/*inHalfHeightOfCylinder=*/0.5f * cCharacterHeightStanding, /*inRadius=*/cCharacterRadiusStanding)).Create().Get();

	// Create 'player' character
	JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
	settings->mMaxSlopeAngle = JPH::DegreesToRadians(45.0f);
	settings->mShape = mStandingShape;
	settings->mUp = JPH::Vec3(0, 0, 1); // Set world-space up vector
	settings->mSupportingVolume = JPH::Plane(JPH::Vec3(0,0,1), -SPHERE_RAD); // Accept contacts that touch the lower sphere of the capsule
	//settings->mCharacterPadding = 0.5f;
	jolt_character = new JPH::CharacterVirtual(settings, toJoltVec3(initial_player_pos), JPH::Quat::sIdentity(), physics_world.physics_system);
	jolt_character->SetUp(JPH::Vec3(0, 0, 1)); // Set world-space up vector
	
#if 0
	JPH::RefConst<JPH::Shape> mStandingShape = JPH::RotatedTranslatedShapeSettings(
		JPH::Vec3(0, 0, 0.5f * cCharacterHeightStanding + cCharacterRadiusStanding), // position
		JPH::Quat::sRotation(JPH::Vec3(1,0,0), Maths::pi_2<float>()), // rotate capsule from extending in the y-axis to the z-axis. // JPH::Quat::sIdentity(),
		//JPH::Quat::sIdentity(),
		new JPH::CapsuleShape(/*inHalfHeightOfCylinder=*/0.5f * cCharacterHeightStanding, /*inRadius=*/cCharacterRadiusStanding)).Create().Get();

	// Create 'player' character
	JPH::Ref<JPH::CharacterSettings> settings = new JPH::CharacterSettings();
	settings->mMaxSlopeAngle = JPH::DegreesToRadians(45.0f);
	settings->mLayer = Layers::MOVING;
	settings->mShape = mStandingShape;
	settings->mFriction = 0.5f;
	settings->mUp = JPH::Vec3(0, 0, 1); // Set world-space up vector
	jolt_character = new JPH::Character(settings, JPH::Vec3(0, 0, 10), JPH::Quat::sIdentity(), 0, physics_world.physics_system);
	jolt_character->SetUp(JPH::Vec3(0, 0, 1)); // Set world-space up vector
	jolt_character->AddToPhysicsSystem(JPH::EActivation::Activate);
#endif
#endif // USE_JOLT_PLAYER_PHYSICS
}


void PlayerPhysics::shutdown()
{
#if USE_JOLT_PLAYER_PHYSICS
	//jolt_character->RemoveFromPhysicsSystem();
	jolt_character = NULL;
#endif
}


void PlayerPhysics::setPosition(const Vec3d& new_player_pos) // Move discontinuously.  For teleporting etc.
{
	jolt_character->SetPosition(toJoltVec3(new_player_pos));
}


inline float doRunFactor(bool runpressed)
{
	if(runpressed)
		return runfactor;
	else
		return 1.0f;
}


void PlayerPhysics::processMoveForwards(float factor, bool runpressed, CameraController& cam)
{
	last_runpressed = runpressed;
	moveimpulse += ::toVec3f(cam.getForwardsVec()) * factor * movespeed * doRunFactor(runpressed);
}


void PlayerPhysics::processStrafeRight(float factor, bool runpressed, CameraController& cam)
{
	last_runpressed = runpressed;
	moveimpulse += ::toVec3f(cam.getRightVec()) * factor * movespeed * doRunFactor(runpressed);

}


void PlayerPhysics::processMoveUp(float factor, bool runpressed, CameraController& cam)
{
	last_runpressed = runpressed;
	if(flymode)
		moveimpulse += Vec3f(0,0,1) * factor * movespeed * doRunFactor(runpressed);
}


void PlayerPhysics::processJump(CameraController& cam)
{
	jumptimeremaining = JUMP_PERIOD;
}


void PlayerPhysics::setFlyModeEnabled(bool enabled)
{
	flymode = enabled;
}


static const Vec3f doSpringRelaxation(const std::vector<SpringSphereSet>& springspheresets,
										bool constrain_to_vertical, bool do_fast_mode);


/*static const std::string getGroundStateName(JPH::CharacterBase::EGroundState s)
{
	switch(s)
	{
	case JPH::CharacterBase::EGroundState::OnGround: return "OnGround";
	case JPH::CharacterBase::EGroundState::OnSteepGround: return "OnSteepGround";
	case JPH::CharacterBase::EGroundState::NotSupported: return "NotSupported";
	case JPH::CharacterBase::EGroundState::InAir: return "InAir";
	default: return "unknown";
	}
}*/


UpdateEvents PlayerPhysics::update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float raw_dtime, Vec4f& campos_in_out)
{
	//return UpdateEvents(); // TEMP HACK IMPORTANT for car physics

	//PlatformUtils::Sleep(30); // TEMP HACK
	//raw_dtime *= 0.3;

	const float dtime = myMin(raw_dtime, 0.1f); // Put a cap on dtime, so that if there is a long pause between update() calls for some reason (e.g. loading objects), 
	//printVar(onground);
	//conPrint("lastgroundnormal: " + lastgroundnormal.toString());

	UpdateEvents events;
#if USE_JOLT_PLAYER_PHYSICS

	
	//printVar(vel);

	//conPrint("onground: " + boolToString(onground));

	//-----------------------------------------------------------------
	//apply movement forces
	//-----------------------------------------------------------------
	if(!flymode) // if not flying
	{
		const bool consider_on_ground = (jolt_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);
		if(/*onground*/consider_on_ground)
		{
			//-----------------------------------------------------------------
			//restrict movement to parallel to plane standing on,
			//otherwise will 'take off' from downwards sloping surfaces when walking.
			//-----------------------------------------------------------------
			//Vec3f parralel_impulse = moveimpulse;
			//parralel_impulse.removeComponentInDir(lastgroundnormal);
			//dvel += parralel_impulse;

			//vel = parralel_impulse;


			JPH::Vec3 normal = jolt_character->GetGroundNormal();
			//if(just_climbed_step)
				normal = JPH::Vec3(0,0,1); // HACK  If this is not(0,0,1), fly off top of steps after step climbing

			if(normal != JPH::Vec3(0, 0, 0)) // If ground normal is valid:
			{
				Vec3f ground_normal(normal.GetX(), normal.GetY(), normal.GetZ());

				Vec3f parralel_impulse = moveimpulse;
				parralel_impulse.removeComponentInDir(ground_normal);

				//vel = moveimpulse; // TEMP HACK parralel_impulse;
				vel = parralel_impulse;
			}

			//------------------------------------------------------------------------
			//add the velocity of the object we are standing on
			//------------------------------------------------------------------------
			//if(lastgroundagent)
			//{
			//	vel += lastgroundagent->getVelocity(toVec3f(campos_out));
			//}
		}

		//-----------------------------------------------------------------
		//apply gravity
		//-----------------------------------------------------------------
		Vec3f dvel(0, 0, -9.81f);
		//Vec3f dvel(0, 0, 0);

		if(!/*onground*/consider_on_ground)
		{
			//-----------------------------------------------------------------
			//restrict move impulse to horizontal plane
			//-----------------------------------------------------------------
			Vec3f horizontal_impulse = moveimpulse;
			horizontal_impulse.z = 0;
			//horizontal_impulse.removeComponentInDir(lastgroundnormal);
			dvel += horizontal_impulse;

			//-----------------------------------------------------------------
			//restrict move impulse to length maxairspeed ms^-2
			//-----------------------------------------------------------------
			Vec2f horiz_vel(dvel.x, dvel.y);
			if(horiz_vel.length() > maxairspeed)
				horiz_vel.setLength(maxairspeed);

			dvel.x = horiz_vel.x;
			dvel.y = horiz_vel.y;
		}

		vel += dvel * dtime;

		if(vel.z < -100) // cap falling speed at 100 m/s
			vel.z = -100;
	}
	else
	{
		// Desired velocity is maintaining the current speed but pointing in the moveimpulse direction.
		const float speed = vel.length();
		const Vec3f desired_vel = (moveimpulse.length() < 1.e-4f) ? Vec3f(0.f) : (normalise(moveimpulse) * speed);

		const Vec3f accel = moveimpulse * 3.f
			+ (desired_vel - vel) * 2.f;

		vel += accel * dtime;
	}

	// From CharacterVirtualTest::HandleInput():
	
	//new_velocity += mPhysicsSystem->GetGravity() * inDeltaTime;
	// Smooth the player input
	//mDesiredVelocity = 0.25f * inMovementDirection * cCharacterSpeed + 0.75f * mDesiredVelocity;
	//JPH::Vec3 mDesiredVelocity(moveimpulse.x, moveimpulse.y, 0.f/*moveimpulse.z*/);
	JPH::Vec3 mDesiredVelocity(vel.x, vel.y, vel.z);

	// True if the player intended to move
	//mAllowSliding = !inMovementDirection.IsNearZero();
	//bool mAllowSliding = false;

	// Cancel movement in opposite direction of normal when sliding
	JPH::CharacterVirtual::EGroundState ground_state = jolt_character->GetGroundState();
	//conPrint("ground_state: " + getGroundStateName(ground_state));
	this->onground = ground_state == JPH::CharacterVirtual::EGroundState::OnGround;

	if(ground_state != JPH::CharacterVirtual::EGroundState::InAir)
	{
		//conPrint("Ground contact point: " + toVec4fVec(jolt_character->GetGroundPosition()).toStringNSigFigs(4));
		//conPrint("Ground contact normal: " + toVec4fVec(jolt_character->GetGroundNormal()).toStringNSigFigs(4));
	}
	//printVar((int)ground_state);
	//JPH::Vec3 desired_velocity = mDesiredVelocity;
	//if(ground_state == JPH::CharacterVirtual::EGroundState::OnSteepGround)
	//{
	//	JPH::Vec3 normal = jolt_character->GetGroundNormal();
	//	normal.SetZ(0.0f);
	//	float dot = normal.Dot(desired_velocity);
	//	if(dot < 0.0f)
	//		desired_velocity -= (dot * normal) / normal.LengthSq();
	//}

	//JPH::Vec3 current_vertical_velocity = JPH::Vec3(0, 0, jolt_character->GetLinearVelocity().GetZ());

	JPH::Vec3 ground_velocity = jolt_character->GetGroundVelocity();

	//JPH::Vec3 new_velocity;
	if(ground_state == JPH::CharacterVirtual::EGroundState::OnGround // If on ground
		/*&& (vel.z - ground_velocity.GetZ()) < 0.1f*/) // And not moving away from ground
	{
		// Assume velocity of ground when on ground
		//new_velocity = ground_velocity;

		// Jump
		if(jumptimeremaining > 0)
		{
			conPrint("JUMPING");
			//onground = false;

			// Recompute vel using proper ground normal
			const Vec3f ground_normal = toVec3f(jolt_character->GetGroundNormal());
			vel = removeComponentInDir(moveimpulse, ground_normal) + 
				Vec3f(0, 0, jumpspeed);

			jumptimeremaining = -1;
			events.jumped = true;
			time_since_on_ground = 1; // Hack this up to a large value so jump animation can play immediately.
		}
	}
	//else
	//	new_velocity = JPH::Vec3(0, 0, 0); // current_vertical_velocity;

	jumptimeremaining -= dtime;

	// Gravity
	//if(!flymode)
	//	new_velocity += physics_world.physics_system->GetGravity() * dtime;

	// Player input
	//new_velocity += desired_velocity;

	//conPrint("new_velocity: " + toString(new_velocity.GetX()) + ", " + toString(new_velocity.GetY()) + ", " + toString(new_velocity.GetZ()));
	// Update the velocity
	jolt_character->SetLinearVelocity(JPH::Vec3(vel.x, vel.y, vel.z));






	// Remember old position
	JPH::Vec3 old_position = jolt_character->GetPosition();

	// Track that on ground before the update
	//bool ground_to_air = jolt_character->GetGroundState() != JPH::CharacterBase::EGroundState::InAir;
	bool ground_to_air = jolt_character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround || jolt_character->GetGroundState() == JPH::CharacterBase::EGroundState::OnSteepGround;

	// Update the character position (instant, do not have to wait for physics update)
	jolt_character->Update(dtime, physics_world.physics_system->GetGravity(), physics_world.physics_system->GetDefaultBroadPhaseLayerFilter(Layers::MOVING), 
		physics_world.physics_system->GetDefaultLayerFilter(Layers::MOVING), { }, *physics_world.temp_allocator);


	// ... and that we got into air after
	if(jolt_character->GetGroundState() != JPH::CharacterBase::EGroundState::InAir)
		ground_to_air = false;

	// Calculate effective velocity
	JPH::Vec3 new_position = jolt_character->GetPosition();
	JPH::Vec3 velocity = (new_position - old_position) / dtime;

	//conPrint("Effective velocity from jolt: " + toVec4fVec(velocity).toStringNSigFigs(4));
	this->vel = Vec3f(velocity.GetX(), velocity.GetY(), velocity.GetZ());
	// Get the velocity from jolt before we do stair climbing.  Otherwise the discontinuous movement of stair climbing
	// can result in extremely large velocities.

	//---------------------------Do stair walking-------------------------------------------------
	//just_climbed_step = false;
	if(true)
	{
		const JPH::Vec3 cStepUpHeight = JPH::Vec3(0.0f, 0.0f, 0.4f);
		const float cMinStepForward = 0.02f;
		const float cStepForwardTest = 0.15f;

		// Calculate how much we wanted to move horizontally
		JPH::Vec3 desired_horizontal_step = mDesiredVelocity * dtime;
		float desired_horizontal_step_len = desired_horizontal_step.Length();
		if(desired_horizontal_step_len > 0.0f)
		{
			// Calculate how much we moved horizontally
			JPH::Vec3 achieved_horizontal_step = jolt_character->GetPosition() - old_position;
			achieved_horizontal_step.SetZ(0);

			// Only count movement in the direction of the desired movement
			// (otherwise we find it ok if we're sliding downhill while we're trying to climb uphill)
			JPH::Vec3 step_forward_normalized = desired_horizontal_step / desired_horizontal_step_len;
			achieved_horizontal_step = myMax(0.0f, achieved_horizontal_step.Dot(step_forward_normalized)) * step_forward_normalized;
			float achieved_horizontal_step_len = achieved_horizontal_step.Length();

			// If we didn't move as far as we wanted and we're against a slope that's too steep
			if(achieved_horizontal_step_len + 1.0e-4f < desired_horizontal_step_len
				&& jolt_character->CanWalkStairs(mDesiredVelocity))
			{
				// CanWalkStairs should not have returned true if we are in air
				assert(!ground_to_air);

				// Calculate how much we should step forward
				// Note that we clamp the step forward to a minimum distance. This is done because at very high frame rates the delta time
				// may be very small, causing a very small step forward. If the step becomes small enough, we may not move far enough
				// horizontally to actually end up at the top of the step.
				JPH::Vec3 step_forward = step_forward_normalized * myMax(cMinStepForward, desired_horizontal_step_len - achieved_horizontal_step_len);

				// Calculate how far to scan ahead for a floor. This is only used in case the floor normal at step_forward is too steep.
				// In that case an additional check will be performed at this distance to check if that normal is not too steep.
				JPH::Vec3 step_forward_test = step_forward_normalized * cStepForwardTest;

				jolt_character->WalkStairs(dtime, /*physics_world.physics_system->GetGravity(), */cStepUpHeight, step_forward, step_forward_test, JPH::Vec3::sZero(),
					physics_world.physics_system->GetDefaultBroadPhaseLayerFilter(Layers::MOVING), physics_world.physics_system->GetDefaultLayerFilter(Layers::MOVING), { }, *physics_world.temp_allocator);
			}
		}
	}
	//--------------------------End stair walking--------------------------------------------------

	

	

	if(true)
	{
		// If we're in air for the first frame and we're not moving up, stick to the floor
		if(ground_to_air && (velocity.GetZ() <= 1.0e-6f) && !events.jumped)
		{
			//conPrint("!!!!!!!!!!!Sticking to floor");
			jolt_character->StickToFloor(JPH::Vec3(0, 0, -0.5f), physics_world.physics_system->GetDefaultBroadPhaseLayerFilter(Layers::MOVING), 
				physics_world.physics_system->GetDefaultLayerFilter(Layers::MOVING), { }, *physics_world.temp_allocator);
		}
	}



	JPH::Vec3 char_pos = jolt_character->GetPosition();
	campos_in_out = Vec4f(char_pos.GetX(), char_pos.GetY(), char_pos.GetZ() + EYE_HEIGHT, 1.f);


	if(!/*onground*/(jolt_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround))
		time_since_on_ground += dtime;
	else
		time_since_on_ground = 0;


	moveimpulse.set(0, 0, 0);


#else // else if !USE_JOLT_PLAYER_PHYSICS:
	
	// then the user doesn't fly off into space or similar.


	//-----------------------------------------------------------------
	//apply any jump impulse
	//-----------------------------------------------------------------
	if(jumptimeremaining > 0)
	{
		if(onground)
		{
			onground = false;
			vel += Vec3f(0,0,1) * jumpspeed;
			events.jumped = true;

			time_since_on_ground = 1; // Hack this up to a large value so jump animation can play immediately.
		}
	}

	jumptimeremaining -= dtime;
		
	//-----------------------------------------------------------------
	//apply movement forces
	//-----------------------------------------------------------------
	if(!flymode) // if not flying
	{
		if(onground)
		{
			//-----------------------------------------------------------------
			//restrict movement to parallel to plane standing on,
			//otherwise will 'take off' from downwards sloping surfaces when walking.
			//-----------------------------------------------------------------
			Vec3f parralel_impulse = moveimpulse;
			parralel_impulse.removeComponentInDir(lastgroundnormal);
			//dvel += parralel_impulse;

			vel = parralel_impulse;

			//------------------------------------------------------------------------
			//add the velocity of the object we are standing on
			//------------------------------------------------------------------------
			//if(lastgroundagent)
			//{
			//	vel += lastgroundagent->getVelocity(toVec3f(campos_out));
			//}
		}

		//-----------------------------------------------------------------
		//apply gravity
		//-----------------------------------------------------------------
		Vec3f dvel(0, 0, -9.81f);

		if(!onground)
		{
			//-----------------------------------------------------------------
			//restrict move impulse to horizontal plane
			//-----------------------------------------------------------------
			Vec3f horizontal_impulse = moveimpulse;
			horizontal_impulse.z = 0;
			//horizontal_impulse.removeComponentInDir(lastgroundnormal);
			dvel += horizontal_impulse;

			//-----------------------------------------------------------------
			//restrict move impulse to length maxairspeed ms^-2
			//-----------------------------------------------------------------
			Vec2f horiz_vel(dvel.x, dvel.y);
			if(horiz_vel.length() > maxairspeed)
				horiz_vel.setLength(maxairspeed);

			dvel.x = horiz_vel.x;
			dvel.y = horiz_vel.y;
		}

		vel += dvel * dtime;

		if(vel.z < -100) // cap falling speed at 100 m/s
			vel.z = -100;
	}
	else
	{
		// Desired velocity is maintaining the current speed but pointing in the moveimpulse direction.
		const float speed = vel.length();
		const Vec3f desired_vel = (moveimpulse.length() < 1.e-4f) ? Vec3f(0.f) : (normalise(moveimpulse) * speed);

		const Vec3f accel = moveimpulse * 3.f
		 + (desired_vel - vel) * 2.f;

		vel += accel * dtime;
	}

	//if(onground)
	//	debugPrint("onground."); 
	
	onground = false;
	//lastgroundagent = NULL;
	
	//-----------------------------------------------------------------
	//'integrate' to find new pos (Euler integration)
	//-----------------------------------------------------------------
	Vec3f dpos = vel*dtime;

	Vec3f campos = toVec3f(campos_in_out) + Vec3f(0, 0, campos_z_delta); // Physics/actual campos is below camera campos.

	//campos_z_delta = myMax(0.f, campos_z_delta - 2.f * dtime); // Linearly reduce campos_z_delta over time until it reaches 0.
	campos_z_delta = myMax(0.f, campos_z_delta - 20.f * dtime * campos_z_delta); // Exponentially reduce campos_z_delta over time until it reaches 0.

	for(int i=0; i<5; ++i)
	{	
		if(dpos != Vec3f(0,0,0))
		{		
			//conPrint("-----dpos: " + dpos.toString() + "----");
			//conPrint("iter: " + toString(i));

			//-----------------------------------------------------------------
			//do a trace along desired movement path to see if obstructed
			//-----------------------------------------------------------------
			float closest_dist = 1e9f;
			Vec4f closest_hit_pos_ws(0.f);
			Vec3f hit_normal;
			bool closest_point_in_tri = false;
			bool hitsomething = false;

			for(int s=0; s<3; ++s)//for each sphere in body
			{
				//-----------------------------------------------------------------
				//calc initial sphere position
				//-----------------------------------------------------------------
				// NOTE: The order of these spheres actually makes a difference, even though it shouldn't.
				// When s=0 is the bottom sphere, hit_normal may end up as (0,0,1) from a distance=0 hit, which means on_ground is set and spring relaxation is constrained to z-dir, which results in getting stuck.
				// So instead make sphere 0 the top sphere.
				// NEW: Actually removing the constrain-to-vertical constraint in sphere relaxation makes this not necessary.
				//const Vec3f spherepos = Vec3f(campos.x, campos.y, campos.z - EYE_HEIGHT + SPHERE_RAD * (1 + 2 * s));
				const Vec3f spherepos = Vec3f(campos.x, campos.y, campos.z - EYE_HEIGHT + SPHERE_RAD * (5 - 2 * s));

				const js::BoundingSphere playersphere(spherepos.toVec4fPoint(), SPHERE_RAD);

				//-----------------------------------------------------------------
				//trace sphere through world
				//-----------------------------------------------------------------
				SphereTraceResult traceresults;
				physics_world.traceSphere(playersphere, dpos.toVec4fVector(), traceresults);

				if(traceresults.hit_object)
				{
					//assert(traceresults.fraction >= 0 && traceresults.fraction <= 1);

					const float distgot = traceresults.hitdist_ws;
					printVar(distgot);

					if(distgot < closest_dist)
					{
						hitsomething = true;
						closest_dist = distgot;
						closest_hit_pos_ws = traceresults.hit_pos_ws;
						hit_normal = toVec3f(traceresults.hit_normal_ws);
						closest_point_in_tri = traceresults.point_in_tri;
						assert(hit_normal.isUnitLength());

						//void* userdata = traceresults.hit_object->getUserdata();

						/*if(userdata)
						{
							Agent* agenthit = static_cast<Agent*>(userdata);
							lastgroundagent = agenthit;
						}*/
					}
				}
			}

			if(hitsomething) // if any of the spheres hit something:
			{
				//const float movedist = closest_dist;//max(closest_dist - 0.0, 0);
				//const float usefraction = movedist / dpos.length();
				const float usefraction = closest_dist / dpos.length();
				assert(usefraction >= 0 && usefraction <= 1);

				//debugPrint("traceresults.fraction: " + toString(traceresults.fraction));

		
				campos += dpos * usefraction; // advance camera position
				
				dpos *= (1.0f - usefraction); // reduce remaining distance by dist moved cam.

				//---------------------------------- Do stair climbing ----------------------------------
				// This is done by detecting if we have hit the edge of a step.
				// A step hit is categorised as any hit that is within a certain distance above the ground/foot level.
				// If we do hit a step, we displace the player upwards to just above the step, so it can continue its movement forwards over the step without obstruction.
				// Work out if we hit the edge of a step
				const float foot_z = campos[2] - EYE_HEIGHT;
				const float hitpos_height_above_foot = closest_hit_pos_ws[2] - foot_z;

				//bool hit_step = false;
				if(!closest_point_in_tri && hitpos_height_above_foot > 0.003f && hitpos_height_above_foot < 0.25f)
				{
					//hit_step = true;

					const float jump_up_amount = hitpos_height_above_foot + 0.01f; // Distance to displace the player upwards

					// conPrint("hit step (hitpos_height_above_foot: " + doubleToStringNSigFigs(hitpos_height_above_foot, 4) + "), jump_up_amount: " + doubleToStringNSigFigs(jump_up_amount, 4));

					// Trace a sphere up to see if we can raise up the avatar over the step without obstruction (we don't want to displace the head upwards into an overhanging object)
					const Vec3f spherepos = Vec3f(campos.x, campos.y, campos.z - EYE_HEIGHT + SPHERE_RAD * 5); // Upper sphere centre
					const js::BoundingSphere playersphere(spherepos.toVec4fPoint(), SPHERE_RAD);
					SphereTraceResult traceresults;
					physics_world.traceSphere(playersphere, /*translation_ws=*/Vec4f(0, 0, jump_up_amount, 0), traceresults); // Trace sphere through world

					if(!traceresults.hit_object)
					{
						campos.z += jump_up_amount;
						campos_z_delta = myMin(0.3f, campos_z_delta + jump_up_amount);

						hit_normal = Vec3f(0,0,1); // the step edge normal will be oriented towards the swept sphere centre at the collision point.
						// However consider it pointing straight up, so that next think the player is considered to be on flat ground and hence moves in the x-y plane.
					}
					else
					{
						conPrint("hit an object while tracing sphere up for jump");
					}
				}
				//---------------------------------- End stair climbing ----------------------------------


				const bool was_just_falling = vel.x == 0 && vel.y == 0;

				//-----------------------------------------------------------------
				//kill remaining translation normal to obstructor
				//-----------------------------------------------------------------
				dpos.removeComponentInDir(hit_normal);

				//-----------------------------------------------------------------
				//kill velocity in direction of obstructor normal
				//-----------------------------------------------------------------
				//lastvel = vel;
				vel.removeComponentInDir(hit_normal);

				//-----------------------------------------------------------------
				//if this is an upwards sloping surface, consider it ground.
				//-----------------------------------------------------------------
				if(hit_normal.z > 0.5)
				{
					onground = true;

					//-----------------------------------------------------------------
					//kill all remaining velocity and movement delta, cause the player is
					//now standing on something
					//-----------------------------------------------------------------
					//dpos.set(0,0,0);
					//vel.set(0,0,0);

					lastgroundnormal = hit_normal;

					//-----------------------------------------------------------------
					//kill remaining dpos to prevent sliding down slopes
					//-----------------------------------------------------------------
					if(was_just_falling)
						dpos.set(0,0,0);
				}

				// conPrint("Sphere trace hit something.   hit_normal: " + hit_normal.toString() + ", onground: " + boolToString(onground)); 
			}
			else
			{
				//didn't hit something, so finish all movement.
				campos += dpos;
				dpos.set(0,0,0);
			}	

			//-----------------------------------------------------------------
			//make sure sphere is outside of any object as much as possible
			//-----------------------------------------------------------------
			springspheresets.resize(3);
			for(int s=0; s<3; ++s)//for each sphere in body
			{
				//-----------------------------------------------------------------
				//calc position of sphere
				//-----------------------------------------------------------------
				const Vec3f spherepos = campos - Vec3f(0,0, (EYE_HEIGHT - 1.5f) + (float)s * 0.6f);

				const float REPEL_RADIUS = SPHERE_RAD + 0.005f;//displace to just off the surface
				js::BoundingSphere bigsphere(spherepos.toVec4fPoint(), REPEL_RADIUS);

				springspheresets[s].sphere = bigsphere;
				//-----------------------------------------------------------------
				//get the collision points
				//-----------------------------------------------------------------
				physics_world.getCollPoints(bigsphere, springspheresets[s].collpoints);
				
			}

			// Do a fast pass of spring relaxation.  This is basically just to determine if we are standing on a ground surface.
			Vec3f displacement = doSpringRelaxation(springspheresets, /*constrain to vertical=*/false, /*do_fast_mode=*/true);

			// If we were repelled from an upwards facing surface, consider us to be on the ground.
			if(displacement != Vec3f(0, 0, 0) && normalise(displacement).z > 0.5f)
			{
				//conPrint("repelled from upwards facing surface");
				onground = true;
			}

			// If we are standing on a ground surface, and not trying to move (moveimpulse = 0), then constrain to vertical movement.
			// This prevents sliding down ramps due to relaxation pushing in the normal direction.
			displacement = doSpringRelaxation(springspheresets, /*constrain to vertical=*/onground && (moveimpulse == Vec3f(0.f)), /*do_fast_mode=*/false);
			campos += displacement;
		}
	}

	if(!onground)
		time_since_on_ground += dtime;
	else
		time_since_on_ground = 0;
		
	campos_in_out = (campos - Vec3f(0,0,campos_z_delta)).toVec4fPoint();

	moveimpulse.set(0,0,0);
#endif // end if !USE_JOLT_PLAYER_PHYSICS

	return events;
}


#if !USE_JOLT_PLAYER_PHYSICS
static const Vec3f doSpringRelaxation(const std::vector<SpringSphereSet>& springspheresets,
										bool constrain_to_vertical, bool do_fast_mode)
{
	Vec3f displacement(0,0,0); // total displacement so far of spheres

	int num_iters_done = 0;
	const int max_num_iters = do_fast_mode ? 1 : 100;
	for(int i=0; i<max_num_iters; ++i)
	{	
		num_iters_done++;
		Vec3f force(0,0,0); // sum of forces acting on spheres from all springs
		int numforces = 0; // num forces acting on spheres

		for(size_t s=0; s<springspheresets.size(); ++s)
		{
			const Vec3f currentspherepos = toVec3f(springspheresets[s].sphere.getCenter()) + displacement;

			for(size_t c=0; c<springspheresets[s].collpoints.size(); ++c)
			{
				//-----------------------------------------------------------------
				//get vec from collision point to sphere center == spring vec
				//-----------------------------------------------------------------
				Vec3f springvec = currentspherepos - toVec3f(springspheresets[s].collpoints[c]);
				const float springlen = springvec.normalise_ret_length();

				//::debugPrint("springlen: " + toString(springlen));

				//const float excesslen = springlen - repel_radius;

				//if coll point is inside sphere...
				if(springlen < springspheresets[s].sphere.getRadius())
				{
					//force = springvec * dist coll point is inside sphere
					force += springvec * (springspheresets[s].sphere.getRadius() - springlen);
					++numforces;
				}


				//if(excesslen < 0)
				//{
				//	force += springvec * excesslen * -1.0f;
				//}
			}
		}
		
		if(numforces != 0)
			force /= (float)numforces;

		//NEWCODE: do constrain to vertical movement
		if(constrain_to_vertical)
		{
			force.x = force.y = 0;
		}

		//-----------------------------------------------------------------
		//check for sufficient convergence
		//-----------------------------------------------------------------
		if(force.length2() < 0.0001*0.0001)
			break;

		displacement += force * 0.3f;//0.1;//TEMP was 0.1
	}

	// int numsprings = 0;
	// for(int s=0; s<springspheresets.size(); ++s)
	// 	numsprings += (int)springspheresets[s].collpoints.size();
	// 
	// conPrint("springs took " + toString(num_iters_done) + " iterations to solve for " + toString(numsprings) + " springs"); 

	return displacement;
}
#endif // end if !USE_JOLT_PLAYER_PHYSICS


void PlayerPhysics::debugGetCollisionSpheres(const Vec4f& campos, std::vector<js::BoundingSphere>& spheres_out)
{
	spheres_out.resize(0);
	for(int s=0; s<3; ++s)//for each sphere in body
	{
		//-----------------------------------------------------------------
		//calc position of sphere
		//-----------------------------------------------------------------
		const Vec3f spherepos = Vec3f(campos) - Vec3f(0,0, (EYE_HEIGHT - 1.5f) + (float)s * 0.6f);
		spheres_out.push_back(js::BoundingSphere(spherepos.toVec4fPoint(), SPHERE_RAD));
	}


	// Add contact points from springspheresets, visualise as smaller spheres.
	for(size_t s=0; s<springspheresets.size(); ++s)
	{
		const SpringSphereSet& set = springspheresets[s];
		for(size_t i=0; i<set.collpoints.size(); ++i)
		{
			const Vec4f& pos = set.collpoints[i];
			spheres_out.push_back(js::BoundingSphere(pos, 0.05f));
		}
	}
}

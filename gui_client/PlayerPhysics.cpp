/*=====================================================================
PlayerPhysics.cpp
-----------------
Copyright Glare Technologies Limited 2016 -
File created by ClassTemplate on Mon Sep 23 15:14:04 2002
=====================================================================*/
#include "PlayerPhysics.h"


#include "CameraController.h"
#include "PhysicsWorld.h"
#include "PhysicsObject.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"


static const float runfactor = 5; // How much faster you move when the run button (shift) is held down.
static const float movespeed = 3;
static const float jumpspeed = 4.5;
static const float maxairspeed = 8;


static const float JUMP_PERIOD = 0.1f;


PlayerPhysics::PlayerPhysics()
:	vel(0,0,0),
	moveimpulse(0,0,0),
	lastgroundnormal(0,0,1),
	lastvel(0,0,0),
	jumptimeremaining(0),
	onground(false),
	flymode(false)
{
}


PlayerPhysics::~PlayerPhysics()
{
}


//void PlayerPhysics::preDestroy()
//{
//	lastgroundagent = NULL;
//}


inline float doRunFactor(bool runpressed)
{
	if(runpressed)
		return runfactor;
	else
		return 1.0f;
}


void PlayerPhysics::processMoveForwards(float factor, bool runpressed, CameraController& cam)
{
	moveimpulse += ::toVec3f(cam.getForwardsVec()) * factor * movespeed * doRunFactor(runpressed);
}


void PlayerPhysics::processStrafeRight(float factor, bool runpressed, CameraController& cam)
{
	moveimpulse += ::toVec3f(cam.getRightVec()) * factor * movespeed * doRunFactor(runpressed);

}


void PlayerPhysics::processMoveUp(float factor, bool runpressed, CameraController& cam)
{
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


const Vec3f doSpringRelaxation(const std::vector<SpringSphereSet>& springspheresets,
										bool constrain_to_vertical);


void PlayerPhysics::update(PhysicsWorld& physics_world, float dtime, ThreadContext& thread_context, Vec4f& campos_out)
{
	//printVar(onground);
	//conPrint("lastgroundnormal: " + lastgroundnormal.toString());

	//-----------------------------------------------------------------
	//apply any jump impulse
	//-----------------------------------------------------------------
	if(jumptimeremaining > 0)
	{
		if(onground)
		{
			onground = false;
			vel += Vec3f(0,0,1) * jumpspeed;
		}
	}

	jumptimeremaining -= dtime;
		
	//-----------------------------------------------------------------
	//apply movement forces
	//-----------------------------------------------------------------
	if(!flymode)//if not flying
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
		//apply grav
		//-----------------------------------------------------------------
		Vec3f dvel(0,0,0);

		dvel.z -= 9.81f;
			
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

		if(vel.z < -100)//cap falling speed at 100 m/s
			vel.z = -100;
	}
	else
	{
		//else if flymode, no inertia so just set vel to the desired move direction
		vel = moveimpulse;
	}

	//if(onground)
	//	debugPrint("onground."); 
	
	onground = false;
	//lastgroundagent = NULL;
	
	//-----------------------------------------------------------------
	//'integrate' to find new pos (Euler integration)
	//-----------------------------------------------------------------
	Vec3f dpos = vel*dtime;

	Vec3f campos = toVec3f(campos_out);//.toVec3();

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
			Vec3f hit_normal;
			bool hitsomething = false;

			const float SPHERE_RAD = 0.3f;
			const float EYE_HEIGHT = 1.67f;

			for(int s=0; s<3; ++s)//for each sphere in body
			{
				//-----------------------------------------------------------------
				//calc initial sphere position
				//-----------------------------------------------------------------
				const Vec3f spherepos = campos - Vec3f(0,0, (EYE_HEIGHT - 1.5f) + (float)s * 0.6f);

				const js::BoundingSphere playersphere(spherepos.toVec4fPoint(), SPHERE_RAD);

				//-----------------------------------------------------------------
				//trace sphere through world
				//-----------------------------------------------------------------
				RayTraceResult traceresults;
				physics_world.traceSphere(playersphere, dpos.toVec4fVector(), thread_context, traceresults); 

				if(traceresults.hit_object)
				{
					//assert(traceresults.fraction >= 0 && traceresults.fraction <= 1);

					const float distgot = traceresults.hitdist_ws;
					//printVar(distgot);

					if(distgot < closest_dist)
					{
						hitsomething = true;
						closest_dist = distgot;
						hit_normal = toVec3f(traceresults.hit_normal_ws);

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

				//-----------------------------------------------------------------
				//kill remaining translation normal to obstructor
				//-----------------------------------------------------------------
				dpos.removeComponentInDir(hit_normal);

		
				//-----------------------------------------------------------------
				//kill velocity in direction of obstructor normal
				//-----------------------------------------------------------------
				//lastvel = vel;
				const bool was_just_falling = vel.x == 0 && vel.y == 0;
			
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
					//kill remaing dpos to prevent sliding down slopes
					//-----------------------------------------------------------------
					if(was_just_falling)
						dpos.set(0,0,0);
				}

				//debugPrint("hit something."); 
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
				physics_world.getCollPoints(bigsphere, thread_context, springspheresets[s].collpoints);
				
			}

			campos += doSpringRelaxation(springspheresets, onground);
		}
	}
		
	campos_out = campos.toVec4fPoint();

	moveimpulse.set(0,0,0);
}


const Vec3f doSpringRelaxation(const std::vector<SpringSphereSet>& springspheresets,
										bool constrain_to_vertical)
{
	Vec3f displacement(0,0,0);//total displacement so far of spheres

	int num_iters_done = 0;
	for(int i=0; i<100; ++i)
	{	
		num_iters_done++;
		Vec3f force(0,0,0);//sum of forces acting on spheres from all springs
		int numforces = 0;//num forces acting on spheres

		for(int s=0; s<springspheresets.size(); ++s)
		{
			const Vec3f currentspherepos = toVec3f(springspheresets[s].sphere.getCenter()) + displacement;

			for(int c=0; c<springspheresets[s].collpoints.size(); ++c)
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

		//NEWCODE: do constrain to veritcal movement
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

	int numsprings = 0;
	for(int s=0; s<springspheresets.size(); ++s)
		numsprings += (int)springspheresets[s].collpoints.size();

	//conPrint("springs took " + toString(num_iters_done) + " iterations to solve for " + toString(numsprings) + " springs"); 

	return displacement;
}

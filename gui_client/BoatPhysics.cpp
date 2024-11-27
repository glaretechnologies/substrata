/*=====================================================================
BoatPhysics.cpp
---------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "BoatPhysics.h"


#include "PhysicsWorld.h"
#include "PhysicsObject.h"
#include "ParticleManager.h"
#include "JoltUtils.h"
#include <StringUtils.h>
#include <ConPrint.h>
#include <PlatformUtils.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>


BoatPhysics::BoatPhysics(WorldObjectRef object, JPH::BodyID body_id_, BoatPhysicsSettings settings_, ParticleManager* particle_manager_)
{
	world_object = object.ptr();
	body_id = body_id_;
	settings = settings_;
	particle_manager = particle_manager_;
	righting_time_remaining = -1;
	user_in_driver_seat = false;

	if(world_object->physics_object.nonNull())
		world_object->physics_object->use_zero_linear_drag = true; // We will do the drag computations ourself in BoatPhysics::update().
}


BoatPhysics::~BoatPhysics()
{
}


void BoatPhysics::startRightingVehicle()
{
	righting_time_remaining = 2;
}


void BoatPhysics::userEnteredVehicle(int seat_index) // Should set cur_seat_index
{
	assert(seat_index >= 0 && seat_index < (int)getSettings().seat_settings.size());

	if(seat_index == 0)
		user_in_driver_seat = true;

	righting_time_remaining = -1; // Stop righting vehicle
}


void BoatPhysics::userExitedVehicle(int old_seat_index) // Should set cur_seat_index
{
	if(old_seat_index == 0)
		user_in_driver_seat = false;
}


// https://en.wikipedia.org/wiki/Lift_coefficient
static float liftCoeffForCosTheta(float cos_theta)
{
	// Set to 1 if theta < 25
	const float theta = std::acos(cos_theta);
	const float alpha = Maths::pi_2<float>() - theta;

	if(alpha < degreeToRad(25.f))
		return 2;
	else
		return 0;
}


VehiclePhysicsUpdateEvents BoatPhysics::update(PhysicsWorld& physics_world, const PlayerPhysicsInput& physics_input, float dtime)
{
	VehiclePhysicsUpdateEvents events;

	
	float forward = 0.0f, right = 0.0f;

	// Determine acceleration
	if (physics_input.W_down || physics_input.up_down)
		forward = 1.0f;
	else if(physics_input.S_down || physics_input.down_down)
		forward = -1.0f;

	if(physics_input.SHIFT_down) // boost!
		forward *= 2.f;

	forward = myMax(forward, myMax(physics_input.left_trigger, physics_input.right_trigger) * 2.f);

	// Steering
	right = physics_input.axis_left_x;
	if(physics_input.A_down)
		right = -1.0f;
	else if(physics_input.D_down)
		right = 1.0f;


	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();


	//const JPH::Mat44 transform = body_interface.GetCenterOfMassTransform(car_body_id);
	const JPH::Mat44 transform = body_interface.GetWorldTransform(body_id);

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

	const JPH::Quat R_quat = toJoltQuat(settings.script_settings->model_to_y_forwards_rot_2 * settings.script_settings->model_to_y_forwards_rot_1);
	
	const Matrix4f R_inv = ((settings.script_settings->model_to_y_forwards_rot_2 * settings.script_settings->model_to_y_forwards_rot_1).conjugate()).toMatrix();


	const Vec4f forwards_os = R_inv * forwards_y_for;
	const Vec4f right_os = R_inv * right_y_for;
	const Vec4f up_os = crossProduct(right_os, forwards_os);

	const Vec4f right_vec_ws = to_world * right_os;
	const Vec4f forward_vec_ws = to_world * forwards_os;
	const Vec4f up_vec_ws = to_world * up_os;

	const Vec4f linear_vel = toVec4fVec(body_interface.GetLinearVelocity(body_id));

	//const float speed = toVec4fVec(body_interface.GetLinearVelocity(body_id)).length();
	const float fowards_vel = dot(linear_vel, forward_vec_ws);

	if(user_in_driver_seat)
	{
		// On user input, assure that the car is active
		if (right != 0.0f || forward != 0.0f)
			body_interface.ActivateBody(body_id);

		const Vec4f propellor_point = to_world * settings.script_settings->propellor_point_os;

		if(forward != 0)
		{
			const Vec4f forwards_control_force = forward_vec_ws * settings.script_settings->thrust_force * forward;

			body_interface.AddForce(body_id, toJoltVec3(forwards_control_force), toJoltVec3(propellor_point));

			// Add some foam
			const float propellor_offset = settings.script_settings->propellor_sideways_offset;
			const Vec4f positions[2] = { propellor_point - right_vec_ws * propellor_offset, propellor_point + right_vec_ws * propellor_offset };
			for(int i=0; i<2; ++i)
			{
				for(int z=0; z<4; ++z)
				{
					Particle particle;
					particle.pos = positions[i];
					particle.area = 0.000001f;
					const float xy_spread = 1.f;
					const float vel = physics_input.SHIFT_down ? 7.f : 5.f; //std::fabs(forward) * 5.0f; // Use 'forward' input magnitude for more spray when boosting
					particle.vel = forward_vec_ws * (forward * -5.f) + Vec4f(xy_spread * (-0.5f + rng.unitRandom()), xy_spread * (-0.5f + rng.unitRandom()), rng.unitRandom() * 2, 0) * vel;
					particle.colour = Colour3f(0.6f);
					particle.particle_type = Particle::ParticleType_Foam;
					particle.theta = rng.unitRandom() * Maths::get2Pi<float>();
					particle.width = 1;
					particle.dwidth_dt = 1;
					particle.die_when_hit_surface = true;
					particle_manager->addParticle(particle);
				}
			}
		}

		if(right != 0)
		{
			const Vec4f rudder_deflection_force = right_vec_ws * -right * fowards_vel * settings.script_settings->rudder_deflection_force_factor;
			//const Vec4f thruster_force = right_vec_ws * settings.boat_mass * -0.2 * right;
			const Vec4f total_force = rudder_deflection_force;//TEMP + thruster_force;
			const Vec4f steering_point = propellor_point; // to_world * Vec4f(0,0,0,1) - forward_vec_ws * 3.0f; // TEMP HACK TODO position where rudder should be
			body_interface.AddForce(body_id, toJoltVec3(total_force), toJoltVec3(steering_point));

			//const JPH::Vec3 steering_torque = (desired_angular_vel - angular_vel) * settings.boat_mass * 3.5f;
			//body_interface.AddTorque(body_id, 
		}
		
	} // end if user_in_driver_seat


	//--------- Apply aerodynamic drag and lift forces ------
	float lift_and_drag_submerged_factor;
	//if(world_object->physics_object->last_submerged_volume > 4)
	//	lift_submerged_factor = 1;
	//else if(
		lift_and_drag_submerged_factor = Maths::smoothStep(3.f, 7.f, world_object->physics_object->last_submerged_volume);
		//printVar(lift_and_drag_submerged_factor);

	const float v_mag = linear_vel.length();
	if((lift_and_drag_submerged_factor > 0) && v_mag > 1.0e-3f)
	{
		const Vec4f normed_linear_vel = linear_vel / v_mag;
		
		// Apply drag force
		const float rho = 1020.f; // water density, kg m^-3

		// Compute drag force on front/back of vehicle
		const float forwards_area = settings.script_settings->front_cross_sectional_area; // 2.f; // TEMP HACK APPROX
		const float projected_forwards_area = absDot(normed_linear_vel, forward_vec_ws) * forwards_area;
		const float forwards_C_d = 0.1f; // drag coefficient  (Tesla model S drag coeffcient is about 0.2 apparently)
		const float forwards_F_d = 0.5f * rho * v_mag*v_mag * forwards_C_d * projected_forwards_area;

		// Compute drag force on side of vehicle
		const float side_area = settings.script_settings->side_cross_sectional_area; // 4.f; // TEMP HACK APPROX
		const float projected_side_area = absDot(normed_linear_vel, right_vec_ws) * side_area;
		const float side_C_d = 0.5f; // drag coefficient - roughly that of a sphere
		const float side_F_d = 0.5f * rho * v_mag*v_mag * side_C_d * projected_side_area;

		// Compute drag force on top of vehicle
		const float top_area = settings.script_settings->top_cross_sectional_area; // 8.f; // TEMP HACK APPROX
		const float projected_top_bottom_area = absDot(normed_linear_vel, up_vec_ws) * top_area;
		const float top_C_d = 0.75f; // drag coefficient
		const float top_F_d = 0.5f * rho * v_mag*v_mag * top_C_d * projected_top_bottom_area;

		const float total_F_d = forwards_F_d + side_F_d + top_F_d;
		const Vec4f F_d = normed_linear_vel * -total_F_d * lift_and_drag_submerged_factor;
		body_interface.AddForce(body_id, toJoltVec3(F_d));


		// Compute lift force on bottom/top of vehicle.
		// Lift force is by definition orthogonal to the incoming flow direction, which means orthogonal to the linear velocity in our case.

		const Vec4f lift_force_application_point = toVec4fVec(body_interface.GetCenterOfMassPosition(body_id)) - Vec4f(0,0,1,0); // Lower force application point to under waterline.

		const Vec4f up_lift_force_dir = removeComponentInDir(up_vec_ws, normed_linear_vel);
		if(up_lift_force_dir.length() > 1.0e-3f)
		{
			const float top_dot = dot(normed_linear_vel, up_vec_ws);
			const float projected_top_area = top_dot * top_area;

			// Compute lift from bottom surface - this is an upwards force
			const float projected_bottom_area = -projected_top_area;
			const float bottom_C_L = liftCoeffForCosTheta(-top_dot);
			if(bottom_C_L > 0)
			{
				if(projected_bottom_area > 0)
				{
					const float bottom_L = 0.5f * rho * v_mag*v_mag * projected_bottom_area * bottom_C_L * lift_and_drag_submerged_factor;
					body_interface.AddForce(body_id, toJoltVec3(normalise(up_lift_force_dir) * bottom_L), toJoltVec3(lift_force_application_point));
				}
			}
		}

		const Vec4f right_lift_force_dir = removeComponentInDir(right_vec_ws, normed_linear_vel);
		if(right_lift_force_dir.length() > 1.0e-3f)
		{
			// Compute lift from right surface - this is a leftwards force
			const float right_dot = dot(normed_linear_vel, right_vec_ws);
			const float right_C_L = liftCoeffForCosTheta(right_dot);
			const float projected_right_area = dot(normed_linear_vel, right_vec_ws) * side_area;
			if(projected_right_area > 0)
			{
				const float right_L = 0.5f * rho * v_mag*v_mag * projected_right_area * right_C_L * lift_and_drag_submerged_factor;
				//conPrint("Applying lift force to left: " + toString(right_L));
				body_interface.AddForce(body_id, toJoltVec3(-normalise(right_lift_force_dir) * right_L), toJoltVec3(lift_force_application_point));
			}

			// Compute lift from left surface - this is a rightwards force
			const float projected_left_area = -projected_right_area;
			const float left_C_L = liftCoeffForCosTheta(-right_dot);
			if(projected_left_area > 0)
			{
				const float left_L = 0.5f * rho * v_mag*v_mag * projected_left_area * left_C_L * lift_and_drag_submerged_factor;
				body_interface.AddForce(body_id, toJoltVec3(normalise(right_lift_force_dir) * left_L), toJoltVec3(lift_force_application_point));
			}
		}
	}

	const Vec4f up_ws = Vec4f(0,0,1,0);
	const Vec4f no_roll_vehicle_right_ws = normalise(crossProduct(forward_vec_ws, up_ws));

	if(righting_time_remaining > 0) // If currently righting boat:
	{
		// Get current rotation, compute the desired rotation, which is a rotation with the current yaw but no pitch or roll, 
		// compute a rotation to get from current to desired
		const JPH::Quat current_rot = body_interface.GetRotation(body_id);

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
		const JPH::Vec3 angular_vel = body_interface.GetAngularVelocity(body_id);
		const JPH::Vec3 correction_torque = (desired_angular_vel - angular_vel) * settings.boat_mass * 3.5f;
		body_interface.AddTorque(body_id, correction_torque);

		righting_time_remaining -= dtime;
	}

	//const float speed_km_h = v_mag * (3600.0f / 1000.f);
	//conPrint("speed (km/h): " + doubleToStringNDecimalPlaces(speed_km_h, 1));

	return events;
}


Vec4f BoatPhysics::getFirstPersonCamPos(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const
{
	const Matrix4f seat_to_world = getSeatToWorldTransform(physics_world, seat_index, use_smoothed_network_transform);
	return seat_to_world * Vec4f(0,0,0.6f,1); // Raise camera position to appox head position
}


Vec4f BoatPhysics::getThirdPersonCamTargetTranslation() const
{
	return Vec4f(0, 0, 0.3f, 0);
}


Matrix4f BoatPhysics::getBodyTransform(PhysicsWorld& physics_world) const
{
	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();
	const JPH::Mat44 transform = body_interface.GetWorldTransform(body_id);

	JPH::Float4 cols[4];
	transform.StoreFloat4x4(cols);

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
Matrix4f BoatPhysics::getSeatToWorldTransform(PhysicsWorld& physics_world, uint32 seat_index, bool use_smoothed_network_transform) const
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


Vec4f BoatPhysics::getLinearVel(PhysicsWorld& physics_world) const
{
	JPH::BodyInterface& body_interface = physics_world.physics_system->GetBodyInterface();
	return toVec4fVec(body_interface.GetLinearVelocity(body_id));
}

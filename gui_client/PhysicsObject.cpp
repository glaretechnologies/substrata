/*=====================================================================
PhysicsObject.cpp
-----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "PhysicsObject.h"


#include "PhysicsWorld.h"
#include <simpleraytracer/ray.h>
#include <utils/StringUtils.h>
#include <utils/ConPrint.h>


js::AABBox PhysicsShape::getAABBOS() const
{
	JPH::AABox aabb = jolt_shape->GetLocalBounds();
	return js::AABBox(
		Vec4f(aabb.mMin.GetX(), aabb.mMin.GetY(), aabb.mMin.GetZ(), 1),
		Vec4f(aabb.mMax.GetX(), aabb.mMax.GetY(), aabb.mMax.GetZ(), 1)
	);
}



PhysicsObject::PhysicsObject(bool collidable_)
:	userdata(NULL), userdata_type(0), collidable(collidable_), /*uniform_dist(NULL), total_surface_area(0), */pos(0.f), smooth_translation(0.f), smooth_rotation(Quatf::identity())
{
	dynamic = false;
	kinematic = false;
#if USE_JOLT
	is_sphere = false;
	is_cube = false;
#endif

	mass = 100.f;
	friction = 0.5f;
	restitution = 0.3f;
	use_zero_linear_drag = false;
	underwater = false;
	last_submerged_volume = 0;
}


PhysicsObject::PhysicsObject(bool collidable_, const PhysicsShape& shape_, void* userdata_, int userdata_type_)
:	shape(shape_), collidable(collidable_), userdata(userdata_), userdata_type(userdata_type_), smooth_translation(0.f), smooth_rotation(Quatf::identity())/*, uniform_dist(NULL), total_surface_area(0)*/
{
	dynamic = false;
	kinematic = false;
#if USE_JOLT
	is_sphere = false;
	is_cube = false;
#endif

	mass = 100.f;
	friction = 0.5f;
	restitution = 0.3f;
	use_zero_linear_drag = false;
	underwater = false;
	last_submerged_volume = 0;
}


PhysicsObject::~PhysicsObject()
{
}


const js::AABBox PhysicsObject::getAABBoxWS() const
{
	Matrix4f ob_to_world_, world_to_ob;
	computeToWorldAndToObMatrices(this->pos, this->rot, this->scale.toVec4fVector(), ob_to_world_, world_to_ob);

	return this->shape.getAABBOS().transformedAABBFast(ob_to_world_);
}


const Matrix4f PhysicsObject::getSmoothedObToWorldMatrix() const
{
	Matrix4f ob_to_world_, world_to_ob;
	computeToWorldAndToObMatrices(this->pos + this->smooth_translation, this->smooth_rotation * this->rot, this->scale.toVec4fVector(), ob_to_world_, world_to_ob);
	return ob_to_world_;
}


const Matrix4f PhysicsObject::getSmoothedObToWorldNoScaleMatrix() const
{
	Matrix4f ob_to_world_, world_to_ob;
	computeToWorldAndToObMatrices(this->pos + this->smooth_translation, this->smooth_rotation * this->rot, /*scale=*/Vec4f(1.f), ob_to_world_, world_to_ob);
	return ob_to_world_;
}


const Matrix4f PhysicsObject::getObToWorldMatrix() const
{
	Matrix4f ob_to_world_, world_to_ob;
	computeToWorldAndToObMatrices(this->pos, this->rot, this->scale.toVec4fVector(), ob_to_world_, world_to_ob);
	return ob_to_world_;
}


const Matrix4f PhysicsObject::getObToWorldMatrixNoScale() const
{
	Matrix4f ob_to_world_, world_to_ob;
	computeToWorldAndToObMatrices(this->pos, this->rot, /*scale=*/Vec4f(1.f), ob_to_world_, world_to_ob);
	return ob_to_world_;
}


const Matrix4f PhysicsObject::getWorldToObMatrix() const
{
	Matrix4f ob_to_world_, world_to_ob;
	computeToWorldAndToObMatrices(this->pos, this->rot, this->scale.toVec4fVector(), ob_to_world_, world_to_ob);
	return world_to_ob;
}


void PhysicsObject::traceRay(const Ray& ray, RayTraceResult& results_out) const
{
	assert(0); // Disabled for now

	results_out.hit_t = -1;
	results_out.hit_object = NULL;

#if !USE_JOLT

	// Test ray against object AABB.  Updates ray near and far distances if it intersects the AABB.
	Ray clipped_ray = ray;
	if(this->aabb_ws.traceRay(clipped_ray) == 0)
		return;

	// NOTE: using clipped ray results in missed intersections with a flat plane, due to precision issues.  So just use the original ray below.
	
	const Vec4f dir_os = this->world_to_ob.mul3Vector(ray.unitDirF());
	const Vec4f pos_os = this->world_to_ob.mul3Point(ray.startPosF());

	const Ray localray(
		pos_os, // origin
		dir_os, // direction
		ray.minT(), // min_t - Use the world space ray min_t.
		ray.maxT()
	);

	HitInfo hitinfo;
	const float dist = (float)shape->raymesh->traceRay(
		localray,
		hitinfo
	);

	if(dist > 0)
	{
		results_out.hit_object = this;
		results_out.coords = hitinfo.sub_elem_coords;
		results_out.hit_tri_index = hitinfo.sub_elem_index;
		results_out.hitdist_ws = dist;
		unsigned int mat_index;
		const Vec4f N_os = shape->raymesh->getGeometricNormalAndMatIndex(hitinfo, mat_index);
		results_out.hit_normal_ws = normalise(this->world_to_ob.transposeMult3Vector(N_os));
	}
#endif
}

/*=====================================================================
PhysicsObject.cpp
-----------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "PhysicsObject.h"


#include "PhysicsWorld.h"
#include "../utils/StringUtils.h"
#include "../simpleraytracer/ray.h"
#include "../physics/jscol_boundingsphere.h"


PhysicsObject::PhysicsObject()
:	userdata(NULL)
{
}


PhysicsObject::~PhysicsObject()
{
}


void PhysicsObject::traceRay(const Ray& ray, float max_t, ThreadContext& thread_context, RayTraceResult& results_out) const
{
	results_out.hitdist_ws = -1;

	const Vec4f dir_os = this->world_to_ob.mul3Vector(ray.unitDirF());
	const Vec4f pos_os = this->world_to_ob * ray.startPosF();

	const Ray localray(
		pos_os, // origin
		dir_os, // direction
		ray.minT(), // min_t - Use the world space ray min_t.
		max_t,
		false // shadow ray
	);

	HitInfo hitinfo;
	const float dist = (float)geometry->traceRay(
		localray,
		thread_context,
		hitinfo
	);

	if(dist > 0)
	{
		results_out.hit_object = this;
		results_out.coords = hitinfo.sub_elem_coords;
		results_out.hit_tri_index = hitinfo.sub_elem_index;
		results_out.hitdist_ws = dist;
		unsigned int mat_index;
		results_out.hit_normal_ws = geometry->getGeometricNormalAndMatIndex(hitinfo, mat_index);
	}
}


void PhysicsObject::traceSphere(const js::BoundingSphere& sphere_ws, const Vec4f& translation_ws, const js::AABBox& spherepath_aabb_ws, ThreadContext& thread_context, RayTraceResult& results_out) const
{
	if(spherepath_aabb_ws.disjoint(this->aabb_ws))
	{
		results_out.hitdist_ws = -1;
		return;
	}

	const Vec4f startpos_os    = world_to_ob * sphere_ws.getCenter();
	const Vec4f translation_os = world_to_ob * translation_ws;
	const float sphere_r_os    = (world_to_ob * Vec4f(0,0,sphere_ws.getRadius(),0)).length(); // TEMP HACK: will only work for uniform scaling.

	float translation_len;
	const Vec4f unitdir = normalise(translation_os, translation_len);

	const Ray ray_os(
		startpos_os, // origin
		unitdir, // direction
		0.f, // min_t - Use the world space ray min_t.
		std::numeric_limits<float>::infinity() // max_t
	);

	Vec4f closest_hit_normal;
	const float smallest_dist = (float)geometry->traceSphere(ray_os, sphere_r_os, translation_len, thread_context, closest_hit_normal);

	if(smallest_dist >= 0.f && smallest_dist < std::numeric_limits<float>::infinity())
	{
		assert(closest_hit_normal.isUnitLength());
		assert(smallest_dist <= translation_len);

		results_out.hitdist_ws = (this->ob_to_world * (unitdir * -smallest_dist)).length(); // Get length in ws.  NOTE: incorrect for non-uniform scaling.  Fix.
		results_out.hit_normal_ws = normalise(this->world_to_ob.transposeMult3Vector(closest_hit_normal));
		results_out.hit_tri_index = 0;//TEMP
		results_out.coords = Vec2f(0,0);//TEMP
	}
	else
	{
		results_out.hitdist_ws = -1;
	}
}


void PhysicsObject::appendCollPoints(const js::BoundingSphere& sphere_ws, const js::AABBox& sphere_aabb_ws, ThreadContext& thread_context, std::vector<Vec4f>& points_ws_in_out) const
{
	if(sphere_aabb_ws.disjoint(this->aabb_ws))
		return;

	const Vec4f sphere_centre_os = this->world_to_ob * sphere_ws.getCenter();
	const float sphere_r_os = (world_to_ob * Vec4f(0,0,sphere_ws.getRadius(),0)).length(); // TEMP HACK: will only work for uniform scaling.

	const size_t initial_num = points_ws_in_out.size();
	geometry->appendCollPoints(sphere_centre_os, sphere_r_os, thread_context, points_ws_in_out);

	// Transform points returned from this object from object to world space
	for(size_t i=initial_num; i<points_ws_in_out.size(); ++i)
	{
		points_ws_in_out[i] = this->ob_to_world * points_ws_in_out[i];
		assert(points_ws_in_out[i].getDist(sphere_ws.getCenter()) <= sphere_ws.getRadius());
	}
}


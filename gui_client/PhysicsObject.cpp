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
#include "../utils/ConPrint.h"


PhysicsObject::PhysicsObject(bool collidable_)
:	userdata(NULL), userdata_type(0), collidable(collidable_)
{
}


PhysicsObject::~PhysicsObject()
{
}


void PhysicsObject::traceRay(const Ray& ray, float max_t, RayTraceResult& results_out) const
{
	results_out.hitdist_ws = -1;

	const Vec4f dir_os = this->world_to_ob.mul3Vector(ray.unitDirF());
	const Vec4f pos_os = this->world_to_ob * ray.startPosF();

	const Ray localray(
		pos_os, // origin
		dir_os, // direction
		ray.minT(), // min_t - Use the world space ray min_t.
		max_t
	);

	HitInfo hitinfo;
	const float dist = (float)geometry->traceRay(
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
		const Vec4f N_os = geometry->getGeometricNormalAndMatIndex(hitinfo, mat_index);
		results_out.hit_normal_ws = normalise(this->world_to_ob.transposeMult3Vector(N_os));
	}
}


void PhysicsObject::traceSphere(const js::BoundingSphere& sphere_ws, const Vec4f& translation_ws, const js::AABBox& spherepath_aabb_ws, RayTraceResult& results_out) const
{
	if(!collidable)
	{
		results_out.hitdist_ws = -1;
		return;
	}

	if(spherepath_aabb_ws.disjoint(this->aabb_ws))
	{
		results_out.hitdist_ws = -1;
		return;
	}

	float translation_len_ws;
	const Vec4f unitdir_ws = normalise(translation_ws, translation_len_ws);

	const Ray ray_ws(
		sphere_ws.getCenter(), // origin
		unitdir_ws, // direction
		0.f, // min_t
		translation_len_ws // max_t
	);

	Vec4f closest_hit_normal_ws;
	const float smallest_dist_ws = (float)geometry->traceSphere(ray_ws, world_to_ob, ob_to_world, sphere_ws.getRadius(), closest_hit_normal_ws);

	if(smallest_dist_ws >= 0.f && smallest_dist_ws < std::numeric_limits<float>::infinity())
	{
		assert(closest_hit_normal_ws.isUnitLength());
		assert(smallest_dist_ws <= translation_len_ws);

		results_out.hitdist_ws = smallest_dist_ws;
		results_out.hit_normal_ws = closest_hit_normal_ws;
		results_out.hit_tri_index = 0;//TEMP
		results_out.coords = Vec2f(0,0);//TEMP
	}
	else
	{
		results_out.hitdist_ws = -1;
	}
}


void PhysicsObject::appendCollPoints(const js::BoundingSphere& sphere_ws, const js::AABBox& sphere_aabb_ws, std::vector<Vec4f>& points_ws_in_out) const
{
	if(!collidable)
		return;

	if(sphere_aabb_ws.disjoint(this->aabb_ws))
		return;

	geometry->appendCollPoints(sphere_ws.getCenter(), sphere_ws.getRadius(), world_to_ob, ob_to_world, points_ws_in_out);
}


size_t PhysicsObject::getTotalMemUsage() const
{
	return sizeof(ob_to_world) + sizeof(world_to_ob) + sizeof(aabb_ws) + geometry->getTotalMemUsage();
}

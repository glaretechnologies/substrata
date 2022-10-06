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
#include "indigo/DiscreteDistribution.h"
#include "../utils/ConPrint.h"


PhysicsObject::PhysicsObject(bool collidable_)
:	userdata(NULL), userdata_type(0), collidable(collidable_), uniform_dist(NULL), total_surface_area(0), pos(0.f)//, rot(Quatf::identity()), scale(1.f)
{
	dynamic = false;
	is_sphere = false;
	is_cube = false;
}


PhysicsObject::PhysicsObject(bool collidable_, const Reference<RayMesh>& geometry_, const Matrix4f& ob_to_world_, void* userdata_, int userdata_type_)
:	ob_to_world(ob_to_world_), geometry(geometry_), collidable(collidable_), userdata(userdata_), userdata_type(userdata_type_), uniform_dist(NULL), total_surface_area(0) // , transform_updated_from_physics(false)
{
	dynamic = false;
	is_sphere = false;
	is_cube = false;
}


PhysicsObject::~PhysicsObject()
{
}


void PhysicsObject::traceRay(const Ray& ray, RayTraceResult& results_out) const
{
	results_out.hitdist_ws = -1;
	results_out.hit_object = NULL;

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


void PhysicsObject::traceSphere(const js::BoundingSphere& sphere_ws, const Vec4f& translation_ws, const js::AABBox& spherepath_aabb_ws, SphereTraceResult& results_out) const
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
	if(translation_len_ws < 1.0e-10f)
	{
		results_out.hitdist_ws = -1;
		return; // Avoid using NaN unitdir_ws
	}

	const Ray ray_ws(
		sphere_ws.getCenter(), // origin
		unitdir_ws, // direction
		0.f, // min_t
		translation_len_ws // max_t
	);

	bool point_in_tri;
	Vec4f closest_hit_pos_ws;
	Vec4f closest_hit_normal_ws;
	const float smallest_dist_ws = (float)geometry->traceSphere(ray_ws, world_to_ob, ob_to_world, sphere_ws.getRadius(), closest_hit_pos_ws, closest_hit_normal_ws, point_in_tri);

	if(smallest_dist_ws >= 0.f && smallest_dist_ws < std::numeric_limits<float>::infinity())
	{
		assert(closest_hit_normal_ws.isUnitLength());
		assert(smallest_dist_ws <= translation_len_ws);

		results_out.hit_pos_ws = closest_hit_pos_ws;
		results_out.hit_normal_ws = closest_hit_normal_ws;
		results_out.hitdist_ws = smallest_dist_ws;
		results_out.point_in_tri = point_in_tri;
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


// From Object::buildUniformSampler from indigo source
void PhysicsObject::buildUniformSampler()
{
	if(uniform_dist != NULL) // If already built:
		return;

	std::vector<float> local_sub_elem_surface_areas;
	geometry->getSubElementSurfaceAreas(
		ob_to_world, // A_inverse,
		local_sub_elem_surface_areas
	);

	double A = 0;
	for(size_t i=0; i<local_sub_elem_surface_areas.size(); ++i)
		A += local_sub_elem_surface_areas[i];

	//this->recip_total_surface_area = (float)(1 / A);
	this->total_surface_area = (float)A;

	delete uniform_dist;
	uniform_dist = new DiscreteDistribution(local_sub_elem_surface_areas);
}


void PhysicsObject::sampleSurfaceUniformly(float sample, const Vec2f& samples, SampleSurfaceResults& results) const
{
	assert(this->uniform_dist);

	// Pick sub-element
	float sub_elem_prob;
	results.hitinfo.sub_elem_index = uniform_dist->sample(sample, sub_elem_prob);

	Vec4f pos_os, N_g_os;
	unsigned int material_index;
	Vec2f uv0;
	geometry->sampleSubElement(results.hitinfo.sub_elem_index, samples, pos_os, N_g_os, results.hitinfo, material_index, uv0);

	// Compute results.pd: is just 1 / total surface area as we are sampling uniformly.
	// NOTE TODO: not actually correct for sphere.
	//results.pd = this->recip_total_surface_area;

	//float ob_to_world_det;
	results.N_g_os = normalise(N_g_os);
	results.N_g_ws = normalise(world_to_ob.transposeMult3Vector(N_g_os));
	results.pos = ob_to_world * pos_os;
}

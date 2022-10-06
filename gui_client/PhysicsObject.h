/*=====================================================================
PhysicsObject.h
---------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "../maths/Vec4f.h"
#include "../maths/Quat.h"
#include "../maths/vec3.h"
#include "../maths/Matrix4f.h"
#include "utils/Vector.h"
#include "simpleraytracer/raymesh.h"

#include <Jolt/Jolt.h>
#include <Jolt\Physics\Body\BodyID.h>

namespace js { class BoundingSphere; }
class RayTraceResult;
class SphereTraceResult;
class DiscreteDistribution;


/*=====================================================================
PhysicsObject
-------------

=====================================================================*/
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4324) // Disable 'structure was padded due to __declspec(align())' warning.
#endif

class PhysicsObject : public ThreadSafeRefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	friend class PhysicsWorld;

	PhysicsObject(bool collidable);
	PhysicsObject(bool collidable, const Reference<RayMesh>& geometry, const Matrix4f& ob_to_world, void* userdata, int userdata_type);
	~PhysicsObject();


	void traceRay(const Ray& ray, RayTraceResult& results_out) const;

	void traceSphere(const js::BoundingSphere& sphere, const Vec4f& dir, const js::AABBox& spherepath_aabb_ws, SphereTraceResult& results_out) const;

	void appendCollPoints(const js::BoundingSphere& sphere, const js::AABBox& sphere_aabb_ws, std::vector<Vec4f>& points_ws_in_out) const;

	const js::AABBox& getAABBoxWS() const { return aabb_ws; }

	const js::AABBox getAABBoxOS() const { return geometry->getAABBox(); }

	//void setAABBoxWS(const js::AABBox& aabb) { aabb_ws = aabb; }

	const Matrix4f& getObToWorldMatrix() const { return ob_to_world; }
	const Matrix4f& getWorldToObMatrix() const { return world_to_ob; }

	void buildUniformSampler();

	class SampleSurfaceResults
	{
	public:
		Vec4f pos;
		Vec4f N_g_ws;
		Vec4f N_g_os;
		HitInfo hitinfo;
		//PDType pd;
	};
	void sampleSurfaceUniformly(float sample, const Vec2f& samples, SampleSurfaceResults& results) const;

	size_t getTotalMemUsage() const;

	
	Matrix4f ob_to_world; // Don't update directly after inserting into PhysicsWorld, call PhysicsWorld::setNewObToWorldMatrix instead, which updates the ob_grid.
private:
	// These are computed in PhysicsWorld::computeObjectTransformData().
	Matrix4f world_to_ob;
	js::AABBox aabb_ws;
public:
	//js::AABBox aabb_os;
	

	Reference<RayMesh> geometry;

	bool collidable; // Is this object solid, for the purposes of player physics?

	void* userdata;
	int userdata_type;

	DiscreteDistribution* uniform_dist; // Used for sampling a point on the object surface uniformly wrt. surface area. Built by buildUniformSampler()
	float total_surface_area; // Built when uniform_dist is built.

	Vec4f pos;
	Quatf rot; // Set in PhysicsWorld::think() from Jolt data
	Vec3f scale;
	//bool transform_updated_from_physics;

	JPH::BodyID jolt_body_id;

	bool dynamic;
	bool is_sphere;
	bool is_cube;
private:
	
};

#ifdef _WIN32
#pragma warning(pop)
#endif


typedef Reference<PhysicsObject> PhysicsObjectRef;


struct PhysicsObjectHash
{
	size_t operator() (const PhysicsObjectRef& ob) const
	{
		return (size_t)ob.getPointer() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};

/*=====================================================================
PhysicsObject.h
---------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "../maths/Vec4f.h"
#include "../maths/vec3.h"
#include "../maths/Matrix4f.h"
#include "utils/Vector.h"
#include "simpleraytracer/raymesh.h"
namespace js { class BoundingSphere; }
class RayTraceResult;



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

	PhysicsObject(bool collidable);
	~PhysicsObject();


	void traceRay(const Ray& ray, float max_t, RayTraceResult& results_out) const;

	void traceSphere(const js::BoundingSphere& sphere, const Vec4f& dir, const js::AABBox& spherepath_aabb_ws, RayTraceResult& results_out) const;

	void appendCollPoints(const js::BoundingSphere& sphere, const js::AABBox& sphere_aabb_ws, std::vector<Vec4f>& points_ws_in_out) const;

	const js::AABBox& getAABBoxWS() const { return aabb_ws; }

	size_t getTotalMemUsage() const;

	Matrix4f ob_to_world;
	Matrix4f world_to_ob;
	js::AABBox aabb_ws;

	Reference<RayMesh> geometry;

	bool collidable; // Is this object solid, for the purposes of player physics?

	void* userdata;
	int userdata_type;
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

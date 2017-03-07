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
class PhysicsObject : public RefCounted
{
public:
	INDIGO_ALIGNED_NEW_DELETE

	PhysicsObject();
	~PhysicsObject();


	void traceRay(const Ray& ray, float max_t, ThreadContext& thread_context, RayTraceResult& results_out) const;

	void traceSphere(const js::BoundingSphere& sphere, const Vec4f& dir, const js::AABBox& spherepath_aabb_ws, ThreadContext& thread_context, RayTraceResult& results_out) const;

	void appendCollPoints(const js::BoundingSphere& sphere, const js::AABBox& sphere_aabb_ws, ThreadContext& thread_context, std::vector<Vec4f>& points_ws_in_out) const;

	const js::AABBox& getAABBoxWS() const { return aabb_ws; }

	Matrix4f ob_to_world;
	Matrix4f world_to_ob;
	js::AABBox aabb_ws;

	Reference<RayMesh> geometry;

	void* userdata;
private:
	
};


typedef Reference<PhysicsObject> PhysicsObjectRef;

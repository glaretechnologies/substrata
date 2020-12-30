/*=====================================================================
PhysicsObjectBVH.h
------------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "../maths/Vec4f.h"
#include "../utils/Platform.h"
#include "../utils/Vector.h"
#include <vector>
namespace Indigo { class TaskManager; }
class PhysicsObject;
class PrintOutput;
class ThreadContext;
class HitInfo;
class Ray;


class PhysicsObjectBVHNode
{
public:
	Vec4f x; // (left_min_x, right_min_x, left_max_x, right_max_x)
	Vec4f y; // (left_min_y, right_min_y, left_max_y, right_max_y)
	Vec4f z; // (left_min_z, right_min_z, left_max_z, right_max_z)

	int32 child[2];
	int32 padding[2]; // Pad to 64 bytes.
};


/*=====================================================================
PhysicsObjectBVH
----------------

=====================================================================*/
class PhysicsObjectBVH
{
public:
	PhysicsObjectBVH();
	~PhysicsObjectBVH();

	friend class BVHObjectTreeCallBack;

	typedef float Real;

	Real traceRay(const Ray& ray, Real ray_length, ThreadContext& thread_context, double time, 
		const PhysicsObject*& hitob_out, HitInfo& hitinfo_out) const;

	void build(Indigo::TaskManager& task_manager, PrintOutput& print_output);

	size_t getTotalMemUsage() const;

//private:
	js::Vector<const PhysicsObject*, 16> objects;
	js::Vector<PhysicsObjectBVHNode, 64> nodes;
	js::Vector<const PhysicsObject*, 16> leaf_objects;
	int32 root_node_index;
};

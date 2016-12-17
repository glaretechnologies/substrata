/*=====================================================================
PhysicsObject.cpp
-----------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "PhysicsObject.h"


#include "PhysicsWorld.h"
#include "../utils/stringutils.h"
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
	results_out.hitdist = -1;

	const Vec4f dir_os = this->world_to_ob.mul3Vector(ray.unitDirF());
	const Vec4f pos_os = this->world_to_ob * ray.startPosF();

	const Ray localray(
		pos_os, // origin
		dir_os, // direction
		ray.minT() // min_t - Use the world space ray min_t.
	);

	HitInfo hitinfo;
	const float dist = (float)geometry->traceRay(
		localray,
		max_t,
		thread_context,
		hitinfo
	);

	if(dist > 0)
	{
		results_out.hit_object = this;
		results_out.coords = hitinfo.sub_elem_coords;
		results_out.hit_tri_index = hitinfo.sub_elem_index;
		results_out.hitdist = dist;
		unsigned int mat_index;
		results_out.hit_normal_ws = geometry->getGeometricNormalAndMatIndex(hitinfo, mat_index);
	}
}


void PhysicsObject::traceSphere(const js::BoundingSphere& sphere_ws, const Vec4f& translation_ws, const js::AABBox& spherepath_aabb_ws, RayTraceResult& results_out) const
{
	if(spherepath_aabb_ws.disjoint(this->aabb_ws))
	{
		results_out.hitdist = -1;
		return;
	}

	const Vec4f startpos_os = world_to_ob * sphere_ws.getCenter();
	const Vec4f translation_os = world_to_ob * translation_ws;
	const float sphere_r_os = (world_to_ob * Vec4f(0,0,sphere_ws.getRadius(),0)).length(); // TEMP HACK: will only work for uniform scaling.
	const js::BoundingSphere sphere_os(startpos_os, sphere_r_os);

	Vec4f unitdir = translation_os;
	float translation_len;
	unitdir = normalise(unitdir, translation_len);
	const Vec3f unitdir3 = toVec3f(unitdir);

	const Vec4f sourcePoint = startpos_os;
	const Vec3f sourcePoint3 = toVec3f(startpos_os);

	float smallest_dist = std::numeric_limits<float>::infinity(); // smallest dist until hit a tri
	
	Vec3f closest_hit_normal;

	// TEMP: do linear intersection against all triangles in the mesh
	for(size_t i=0; i<geometry->getTriangles().size(); ++i)
	{
		const js::Triangle& tri = geometry->js_tris[i];

		// Determine the distance from the plane to the sphere center
		float pDist = tri.getTriPlane().signedDistToPoint(sourcePoint3);

		//-----------------------------------------------------------------
		//Invert normal if doing backface collision, so 'usenormal' is always facing
		//towards sphere center.
		//-----------------------------------------------------------------
		Vec3f usenormal = tri.getNormal();
		if(pDist < 0)
		{
			usenormal *= -1;
			pDist *= -1;
		}

		assert(pDist >= 0);

		//-----------------------------------------------------------------
		//check if sphere is heading away from tri
		//-----------------------------------------------------------------
		const float approach_rate = -usenormal.dot(unitdir3);
		if(approach_rate <= 0)
			continue;

		assert(approach_rate > 0);

		// trans_len_needed = dist to approach / dist approached per unit translation len
		const float trans_len_needed = (pDist - sphere_r_os) / approach_rate;

		if(translation_len < trans_len_needed)
			continue; // then sphere will never get to plane

		//-----------------------------------------------------------------
		//calc the point where the sphere intersects with the triangle plane (planeIntersectionPoint)
		//-----------------------------------------------------------------
		Vec3f planeIntersectionPoint;

		// Is the plane embedded in the sphere?
		if(trans_len_needed <= 0)//pDist <= sphere.getRadius())//make == trans_len_needed < 0
		{
			// Calculate the plane intersection point
			planeIntersectionPoint = tri.getTriPlane().closestPointOnPlane(sourcePoint3);

		}
		else
		{
			assert(trans_len_needed >= 0);

			planeIntersectionPoint = sourcePoint3 + (unitdir3 * trans_len_needed) - (sphere_r_os * usenormal);

			//assert point is actually on plane
			assert(epsEqual(tri.getTriPlane().signedDistToPoint(planeIntersectionPoint), 0.0f, 0.0001f));
		}

		//-----------------------------------------------------------------
		//now restrict collision point on tri plane to inside tri if neccessary.
		//-----------------------------------------------------------------
		Vec3f triIntersectionPoint = planeIntersectionPoint;
		
		const bool point_in_tri = tri.pointInTri(triIntersectionPoint);
		if(!point_in_tri)
		{
			//-----------------------------------------------------------------
			//restrict to inside tri
			//-----------------------------------------------------------------
			triIntersectionPoint = tri.closestPointOnTriangle(triIntersectionPoint);
		}


//		if(triIntersectionPoint.getDist2(sourcePoint3) < sphere.getRadius2())
//		{
#ifndef NOT_CYBERSPACE
			//::debugPrint("jscol: WARNING: tri embedded in sphere");
#endif
			//-----------------------------------------------------------------
			//problem, so just ignore this tri :)
			//-----------------------------------------------------------------
			//continue;

//TEMP	smallest_dist = 0;
	//	sphere_hit_a_tri = true;
	//	normal_os_out = usenormal;
	//	break;//don't test against other tris?
//		}

		
		//-----------------------------------------------------------------
		//Using the triIntersectionPoint, we need to reverse-intersect
		//with the sphere
		//-----------------------------------------------------------------
		
		//returns dist till hit sphere or -1 if missed
		//inline float rayIntersect(const Vec3& raystart_os, const Vec3& rayunitdir) const;
		const float dist = sphere_os.rayIntersect(triIntersectionPoint.toVec4fPoint(), -unitdir);
		
		if(dist >= 0 && dist < smallest_dist && dist < translation_len)
		{
			smallest_dist = dist;

			//-----------------------------------------------------------------
			//calc hit normal
			//-----------------------------------------------------------------
			if(point_in_tri)
				closest_hit_normal = usenormal;
			else
			{
				//-----------------------------------------------------------------
				//calc point sphere will be when it hits edge of tri
				//-----------------------------------------------------------------
				const Vec3f hit_spherecenter = sourcePoint3 + unitdir3 * dist;

				//const float d = hit_spherecenter.getDist(triIntersectionPoint);
				assert(epsEqual(hit_spherecenter.getDist(triIntersectionPoint), sphere_r_os));

				closest_hit_normal = (hit_spherecenter - triIntersectionPoint) / sphere_r_os; 
			}
		}

	}//end for each triangle

	if(smallest_dist < std::numeric_limits<float>::infinity())
	{
		results_out.hitdist = smallest_dist;
		results_out.hit_normal_ws = normalise(this->world_to_ob.transposeMult3Vector(closest_hit_normal.toVec4fVector()));
		results_out.hit_object = this;
		results_out.hit_tri_index = 0;//TEMP
		results_out.coords = Vec2f(0,0);//TEMP
	}
	else
	{
		results_out.hit_object = NULL;
	}
}


void PhysicsObject::appendCollPoints(const js::BoundingSphere& sphere_ws, const js::AABBox& sphere_aabb_ws, std::vector<Vec4f>& points_ws_in_out) const
{
	if(sphere_aabb_ws.disjoint(this->aabb_ws))
		return;

	const Vec4f sphere_centre_os = this->world_to_ob * sphere_ws.getCenter();
	const float sphere_r_os = (world_to_ob * Vec4f(0,0,sphere_ws.getRadius(),0)).length(); // TEMP HACK: will only work for uniform scaling.
	const js::BoundingSphere sphere_os(sphere_centre_os, sphere_r_os);

	for(uint32 i = 0; i < geometry->getTriangles().size(); ++i)
	{
		const js::Triangle& tri = geometry->js_tris[i];
		
		//-----------------------------------------------------------------
		//see if sphere is touching plane
		//-----------------------------------------------------------------
		const float disttoplane = tri.getTriPlane().signedDistToPoint(toVec3f(sphere_centre_os));

		if(fabs(disttoplane) > sphere_r_os)
			continue;

		//-----------------------------------------------------------------
		//get closest point on plane to sphere center
		//-----------------------------------------------------------------
		Vec3f planepoint = tri.getTriPlane().closestPointOnPlane(toVec3f(sphere_centre_os));

		//-----------------------------------------------------------------
		//restrict point to inside tri
		//-----------------------------------------------------------------
		if(!tri.pointInTri(planepoint))
		{
			planepoint = tri.closestPointOnTriangle(planepoint);
		}

		if(planepoint.getDist(toVec3f(sphere_centre_os)) <= sphere_r_os)
			points_ws_in_out.push_back(this->ob_to_world * planepoint.toVec4fPoint());
	
	}//end for each triangle
}


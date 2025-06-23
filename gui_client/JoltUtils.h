/*=====================================================================
JoltUtils.h
-----------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <Jolt/Jolt.h>
#include <maths/Vec4.h>


inline JPH::Vec3 toJoltVec3(const Vec4f& v)
{
	// Note that JPH_USE_SSE should be defined
	return JPH::Vec3(v.v);
}

inline JPH::Vec3 toJoltVec3(const Vec3f& v)
{
	return JPH::Vec3(v.x, v.y, v.z);
}

inline JPH::Vec3 toJoltVec3(const Vec3d& v)
{
	return JPH::Vec3((float)v.x, (float)v.y, (float)v.z);
}

inline Vec3f toVec3f(const JPH::Vec3& v)
{
	return Vec3f(v.GetX(), v.GetY(), v.GetZ());
}


inline Vec4f toVec4fVec(const JPH::Vec3& v)
{
	//return Vec4f(v.GetX(), v.GetY(), v.GetZ(), 0.f);
	return maskWToZero(Vec4f(v.mValue));
}

inline static Vec4f toVec4fPos(const JPH::Vec3& v)
{
	//return Vec4f(v.GetX(), v.GetY(), v.GetZ(), 1.f);
	return setWToOne(Vec4f(v.mValue));
}


inline static JPH::Quat toJoltQuat(const Quatf& q)
{
	return JPH::Quat(JPH::Vec4(q.v.v));
}

inline static Quatf toQuat(const JPH::Quat& q)
{
	return Quatf(Vec4f(q.mValue.mValue));
}


inline static Matrix4f toMatrix4f(const JPH::Mat44& mat)
{
	JPH::Float4 cols[4];
	mat.StoreFloat4x4(cols);

	return Matrix4f(&cols[0].x);
}

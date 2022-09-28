/*
A representation of the world.  We might need to add a top-level acceleration structure to speed up sphere tracing and
general queries.

1) To start, we are going to do a linear scan of the all loaded meshes with the sphere tracer.
2) Sphere vs AABB intersection distance and query

*/

import BVH from './bvh';

export default class PhysicsWorld {
  private bvhIndex_: Map<string, BVH>;
  private;

}
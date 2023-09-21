/*=====================================================================
TerrainTests.cpp
----------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "TerrainTests.h"


#include <utils/TaskManager.h>
#include <utils/ContainerUtils.h>
#include <utils/TestUtils.h>


static float world_w = 131072;//8192*4;
static int chunk_res = 127; // quad res per patch
const float quad_w_screenspace_target = 0.004f;
//static const int max_depth = 1;


struct TestGLOb : public RefCounted
{
	int chunk_x, chunk_y, chunk_w;
	uint64 node_id;
};


class MakeTestTerrainChunkTask : public RefCounted
{
public:
	uint64 node_id;
	int chunk_x, chunk_y, chunk_w;
};


class TestTerrainChunkGeneratedMsg
{
public:
	int chunk_x, chunk_y, chunk_w;
	uint64 node_id;
};



typedef Reference<TestGLOb> TestGLObRef;

// Quad-tree node
struct TerrainTestNode : public RefCounted
{
	TerrainTestNode() : building(false), subtree_built(false) {}

	TestGLObRef gl_ob;

	// Objects that have been built, but not inserted into the world yet, because a parent node is waiting for all descendant nodes to finish building.
	TestGLObRef pending_gl_ob;

	TerrainTestNode* parent;

	js::AABBox aabb;
	//int lod_level;
	int depth;
	uint64 id;
	bool building;
	bool subtree_built; // is subtree built?

	int x, y, width;
	
	Reference<TerrainTestNode> children[4];

	std::vector<TestGLObRef> old_subtree_gl_obs; // Objects that are still inserted into opengl engine, to be removed when entire subtree of node has its meshes built.
};


class TerrainTestSystem
{
public:
	TerrainTestSystem(int max_depth_) : next_id(0), max_depth(max_depth_)
	{
		root_node = new TerrainTestNode();
		root_node->parent = NULL;
		root_node->aabb = js::AABBox(Vec4f(-world_w/2, -world_w/2, 0, 1), Vec4f(world_w/2, world_w/2, 0, 1));
		root_node->x = 0;
		root_node->y = 0;
		root_node->width = 1 << max_depth;
		root_node->depth = 0;
		root_node->id = next_id++;
		id_to_node_map[root_node->id] = root_node.ptr();

		const int side_res = 1 << max_depth;
		mesh_covering_square.resize(side_res, side_res);
		mesh_covering_square.setAllElems(std::numeric_limits<uint64>::max());
	}

	Reference<TerrainTestNode> root_node;
	std::map<uint64, TerrainTestNode*> id_to_node_map;
	uint64 next_id;


	// Mock data:
	std::vector<Reference<MakeTestTerrainChunkTask>> tasks;
	std::set<TestGLObRef> obs;

	int max_depth;
	Array2D<uint64> mesh_covering_square;


	//----------------- Mock methods ----------------------
	void addObject(TestGLObRef ob)
	{
		testAssert(obs.count(ob) == 0);

		testAssert(ob->chunk_x >= 0 && (ob->chunk_x + ob->chunk_w) <= mesh_covering_square.getWidth());
		testAssert(ob->chunk_y >= 0 && (ob->chunk_y + ob->chunk_w) <= mesh_covering_square.getHeight());

		// Check no cells have an object in them already, add add to cell
		for(int y=ob->chunk_y; y<ob->chunk_y + ob->chunk_w; ++y)
		for(int x=ob->chunk_x; x<ob->chunk_x + ob->chunk_w; ++x)
		{
			testAssert(mesh_covering_square.elem(x, y) == std::numeric_limits<uint64>::max());

			mesh_covering_square.elem(x, y) = ob->node_id;
		}

		obs.insert(ob);
	}

	void removeObject(TestGLObRef ob)
	{
		testAssert(obs.count(ob) > 0);

		// Check all cells were properly marked as containing this object, then clear cells.
		for(int y=ob->chunk_y; y<ob->chunk_y + ob->chunk_w; ++y)
		for(int x=ob->chunk_x; x<ob->chunk_x + ob->chunk_w; ++x)
		{
			testAssert(mesh_covering_square.elem(x, y) == ob->node_id);

			mesh_covering_square.elem(x, y) = std::numeric_limits<uint64>::max();
		}
		
		obs.erase(ob);
	}

	void addTask(Reference<MakeTestTerrainChunkTask> task)
	{
		tasks.push_back(task);

		conPrint("Added task x=" + toString(task->chunk_x) + ", y=" + toString(task->chunk_y) + ", w=" + toString(task->chunk_w));
	}
	//----------------------------------------------------


	// Test methods
	bool areAllCellsFilled()
	{
		for(int y=0; y<mesh_covering_square.getHeight(); ++y)
		for(int x=0; x<mesh_covering_square.getWidth() ; ++x)
		{
			if(mesh_covering_square.elem(x, y) == std::numeric_limits<uint64>::max()) // If cell empty:
				return false;
		}
		return true;
	}

	size_t numNodes() const { return id_to_node_map.size(); }


	static void appendSubtreeString(TerrainTestNode* node, std::string& s)
	{
		for(int i=0; i<node->depth; ++i)
			s.push_back(' ');

		s += "node, id " + toString(node->id) + " building: " + boolToString(node->building) + ", subtree_built: " + boolToString(node->subtree_built) + ", gl_ob: " + toString((uint64)node->gl_ob.ptr()) + "\n";

		if(node->children[0].nonNull())
		{
			for(int i=0; i<4; ++i)
				appendSubtreeString(node->children[i].ptr(), s);
		}
	}

	void checkInvariantsForSubtree(TerrainTestNode* node)
	{
		if(node->children[0].isNull()) // If leaf node:
		{
		}
		else // else interior:
		{
			testAssert(node->gl_ob.isNull());

			for(int i=0; i<4; ++i)
				checkInvariantsForSubtree(node->children[i].ptr());
		}
	}


	void checkInvariants()
	{
		checkInvariantsForSubtree(root_node.ptr());
	}


	void removeSubtree(TerrainTestNode* node, std::vector<TestGLObRef>& old_subtree_gl_obs_in_out)
	{
		ContainerUtils::append(old_subtree_gl_obs_in_out, node->old_subtree_gl_obs);

		if(node->children[0].isNull()) // If this is a leaf node:
		{
			// Remove mesh for leaf node, if any
			if(node->gl_ob.nonNull())
				old_subtree_gl_obs_in_out.push_back(node->gl_ob);
		}
		else // Else if this node is an interior node:
		{
			// Remove children
			for(int i=0; i<4; ++i)
			{
				removeSubtree(node->children[i].ptr(), old_subtree_gl_obs_in_out);
				id_to_node_map.erase(node->children[i]->id);
				node->children[i] = NULL;
			}
		}
	}

	// The root node of the subtree, 'node', has already been created.
	void createInteriorNodeSubtree(TerrainTestNode* node, const Vec3d& campos)
	{
		// We should split this node into 4 children, and make it an interior node.
		const float cur_w = node->aabb.max_[0] - node->aabb.min_[0];
		const float child_w = cur_w * 0.5f;

		// bot left child
		node->children[0] = new TerrainTestNode();
		node->children[0]->parent = node;
		node->children[0]->depth = node->depth + 1;
		node->children[0]->aabb = js::AABBox(node->aabb.min_, node->aabb.min_ + Vec4f(child_w, child_w, 0, 0));
		node->children[0]->x = node->x;
		node->children[0]->y = node->y;
		node->children[0]->width = node->width / 2;

		// bot right child
		node->children[1] = new TerrainTestNode();
		node->children[1]->parent = node;
		node->children[1]->depth = node->depth + 1;
		node->children[1]->aabb = js::AABBox(node->aabb.min_ + Vec4f(child_w, 0, 0, 0), node->aabb.min_ + Vec4f(2*child_w, child_w, 0, 0));
		node->children[1]->x = node->x + node->width / 2;
		node->children[1]->y = node->y + 0;
		node->children[1]->width = node->width / 2;

		// top right child
		node->children[2] = new TerrainTestNode();
		node->children[2]->parent = node;
		node->children[2]->depth = node->depth + 1;
		node->children[2]->aabb = js::AABBox(node->aabb.min_ + Vec4f(child_w, child_w, 0, 0), node->aabb.min_ + Vec4f(2*child_w, 2*child_w, 0, 0));
		node->children[2]->x = node->x + node->width / 2;
		node->children[2]->y = node->y + node->width / 2;
		node->children[2]->width = node->width / 2;

		// top left child
		node->children[3] = new TerrainTestNode();
		node->children[3]->parent = node;
		node->children[3]->depth = node->depth + 1;
		node->children[3]->aabb = js::AABBox(node->aabb.min_ + Vec4f(0, child_w, 0, 0), node->aabb.min_ + Vec4f(child_w, 2*child_w, 0, 0));
		node->children[3]->x = node->x + 0;
		node->children[3]->y = node->y + node->width / 2;
		node->children[3]->width = node->width / 2;

		// Assign child nodes ids and add to id_to_node_map.
		for(int i=0; i<4; ++i)
		{
			node->children[i]->id = next_id++;
			id_to_node_map[node->children[i]->id] = node->children[i].ptr();
		}

		node->subtree_built = false;

		// Recurse to build child trees
		for(int i=0; i<4; ++i)
			createSubtree(node->children[i].ptr(), campos);
	}

	// The root node of the subtree, 'node', has already been created.
	void createSubtree(TerrainTestNode* node, const Vec3d& campos)
	{
		conPrint("Creating subtree, depth " + toString(node->depth) + ", at " + node->aabb.toStringMaxNDecimalPlaces(4));

		const float min_dist = myMax(1.0e-3f, node->aabb.distanceToPoint(campos.toVec4fPoint()));

		//const int desired_lod_level = myClamp((int)std::log2(quad_w_screenspace_target * min_dist), /*lowerbound=*/0, /*upperbound=*/8);
		// depth = log2(world_w / (res * d * quad_w_screenspace))
		const int desired_depth = myClamp((int)std::log2(world_w / (chunk_res * min_dist * quad_w_screenspace_target)), /*lowerbound=*/0, /*upperbound=*/max_depth);

		//assert(desired_lod_level <= node->lod_level);
		//assert(desired_depth >= node->depth);

		if(desired_depth > node->depth)
		{
			createInteriorNodeSubtree(node, campos);
			//// We should split this node into 4 children, and make it an interior node.
			//const float cur_w = node->aabb.max_[0] - node->aabb.min_[0];
			//const float child_w = cur_w * 0.5f;

			//// bot left child
			//node->children[0] = new TerrainTestNode();
			//node->children[0]->parent = node;
			//node->children[0]->depth = node->depth + 1;
			//node->children[0]->aabb = js::AABBox(node->aabb.min_, node->aabb.min_ + Vec4f(child_w, child_w, 0, 0));
			//node->children[0]->x = node->x;
			//node->children[0]->y = node->y;
			//node->children[0]->width = node->width / 2;

			//// bot right child
			//node->children[1] = new TerrainTestNode();
			//node->children[1]->parent = node;
			//node->children[1]->depth = node->depth + 1;
			//node->children[1]->aabb = js::AABBox(node->aabb.min_ + Vec4f(child_w, 0, 0, 0), node->aabb.min_ + Vec4f(2*child_w, child_w, 0, 0));
			//node->children[1]->x = node->x + node->width / 2;
			//node->children[1]->y = node->y + 0;
			//node->children[1]->width = node->width / 2;

			//// top right child
			//node->children[2] = new TerrainTestNode();
			//node->children[2]->parent = node;
			//node->children[2]->depth = node->depth + 1;
			//node->children[2]->aabb = js::AABBox(node->aabb.min_ + Vec4f(child_w, child_w, 0, 0), node->aabb.min_ + Vec4f(2*child_w, 2*child_w, 0, 0));
			//node->children[2]->x = node->x + node->width / 2;
			//node->children[2]->y = node->y + node->width / 2;
			//node->children[2]->width = node->width / 2;

			//// top left child
			//node->children[3] = new TerrainTestNode();
			//node->children[3]->parent = node;
			//node->children[3]->depth = node->depth + 1;
			//node->children[3]->aabb = js::AABBox(node->aabb.min_ + Vec4f(0, child_w, 0, 0), node->aabb.min_ + Vec4f(child_w, 2*child_w, 0, 0));
			//node->children[3]->x = node->x + 0;
			//node->children[3]->y = node->y + node->width / 2;
			//node->children[3]->width = node->width / 2;

			//// Assign child nodes ids and add to id_to_node_map.
			//for(int i=0; i<4; ++i)
			//{
			//	node->children[i]->id = next_id++;
			//	id_to_node_map[node->children[i]->id] = node->children[i].ptr();
			//}

			//node->subtree_built = false;

			//// Recurse to build child trees
			//for(int i=0; i<4; ++i)
			//	createSubtree(node->children[i].ptr(), campos);
		}
		else
		{
			assert(desired_depth <= node->depth);
			// This node should be a leaf node

			// Create geometry for it
			MakeTestTerrainChunkTask* task = new MakeTestTerrainChunkTask();
			task->node_id = node->id;
			task->chunk_x = node->x;
			task->chunk_y = node->y;
			task->chunk_w = node->width;
			addTask(task);

			node->building = true;
			node->subtree_built = false;
		}
	}


	void updateSubtree(TerrainTestNode* cur, const Vec3d& campos)
	{
		// We want each leaf node to have lod_level = desired_lod_level for that node

		// Get distance from camera to node

		const float min_dist = myMax(1.0e-3f, cur->aabb.distanceToPoint(campos.toVec4fPoint()));

		//const int desired_lod_level = myClamp((int)std::log2(quad_w_screenspace_target * min_dist), /*lowerbound=*/0, /*upperbound=*/8);
		const int desired_leaf_depth = myClamp((int)std::log2(world_w / (chunk_res * min_dist * quad_w_screenspace_target)), /*lowerbound=*/0, /*upperbound=*/max_depth);

		if(cur->children[0].isNull()) // If 'cur' is a leaf node (has no children, so is not interior node):
		{
			if(desired_leaf_depth > cur->depth) // If the desired lod level is greater than the leaf's lod level, we want to split the leaf into 4 child nodes
			{
				// Don't remove leaf geometry from opengl engine yet, wait until subtree geometry is fully built to replace it first.
				if(cur->gl_ob.nonNull())
					cur->old_subtree_gl_obs.push_back(cur->gl_ob);
				cur->gl_ob = NULL;

				createInteriorNodeSubtree(cur, campos);
			}
		}
		else // Else if 'cur' is an interior node:
		{
			if(desired_leaf_depth <= cur->depth) // And it should be a leaf node, or not exist (it is currently too detailed)
			{
				// Change it into a leaf node:
	
				// Remove children of cur and their subtrees
				for(int i=0; i<4; ++i)
				{
					removeSubtree(cur->children[i].ptr(), cur->old_subtree_gl_obs);
					id_to_node_map.erase(cur->children[i]->id);
					cur->children[i] = NULL;
				}
			//}
			//
			//if(desired_leaf_depth == cur->depth)
			//{
				// Start creating geometry for this node:
				// Note that we may already be building geometry for this node, from a previous change from interior node to leaf node.
				// In this case don't make a new task, just wait for existing task.

				assert(cur->gl_ob.isNull());
				if(/*cur->gl_ob.isNull() && */!cur->building)
				{
					// No chunk at this location, make one
					MakeTestTerrainChunkTask* task = new MakeTestTerrainChunkTask();
					task->node_id = cur->id;
					task->chunk_x = cur->x;
					task->chunk_y = cur->y;
					task->chunk_w = cur->width;
					addTask(task);

					conPrint("Making new node chunk");

					cur->subtree_built = false;
					cur->building = true;
				}
			}
			else // Else if 'cur' should still be an interior node:
			{
				assert(cur->children[0].nonNull());
				for(int i=0; i<4; ++i)
					updateSubtree(cur->children[i].ptr(), campos);
			}
		}
	}


	void updateCampos(const Vec3d& campos)
	{
		checkInvariants();

		updateSubtree(root_node.ptr(), campos);

		checkInvariants();
	}


	// The subtree with root node 'node' is fully built, so we can remove and old meshes for it, and insert the new pending meshes.
	void insertPendingMeshesForSubtree(TerrainTestNode* node)
	{
		// Remove any old subtree GL obs and physics obs, now the mesh for this node is ready.
		for(size_t i=0; i<node->old_subtree_gl_obs.size(); ++i)
			removeObject(node->old_subtree_gl_obs[i]);
		node->old_subtree_gl_obs.clear();

		if(node->children[0].isNull()) // If leaf node:
		{
			if(node->pending_gl_ob.nonNull())
			{
				assert(node->gl_ob.isNull());
				node->gl_ob = node->pending_gl_ob;
				addObject(node->gl_ob);
				node->pending_gl_ob = NULL;
			}
		}
		else
		{
			for(int i=0; i<4; ++i)
				insertPendingMeshesForSubtree(node->children[i].ptr());
		}
	}



	bool areAllParentSubtreesBuilt(TerrainTestNode* node)
	{
		TerrainTestNode* cur = node->parent;
		while(cur)
		{
			if(!cur->subtree_built)
				return false;
			cur = cur->parent;
		}

		return true;
	}



	void handleCompletedMakeChunkTask(const TestTerrainChunkGeneratedMsg& msg)
	{
		checkInvariants();

		// Lookup node based on id
		auto res = id_to_node_map.find(msg.node_id);
		if(res != id_to_node_map.end())
		{
			TerrainTestNode& node = *res->second;

			node.building = false;
			if(node.children[0].nonNull()) // If this is an interior node:
				return; // Discard the obsolete built mesh.  This will happen if a leaf node gets converted to an interior node while the mesh is building.

			// This node is a leaf node, and we have the mesh for it, therefore the subtree is complete.
			node.subtree_built = true;


			TestGLObRef gl_ob = new TestGLOb();
			gl_ob->chunk_x = node.x;
			gl_ob->chunk_y = node.y;
			gl_ob->chunk_w = node.width;
			gl_ob->node_id = node.id;

			node.pending_gl_ob = gl_ob;


			if(areAllParentSubtreesBuilt(&node))
			{
				insertPendingMeshesForSubtree(&node);
			}
			else
			{
				TerrainTestNode* cur = node.parent;
				while(cur)
				{
					bool cur_subtree_built = true;
					for(int i=0; i<4; ++i)
						if(!cur->children[i]->subtree_built)
						{
							cur_subtree_built = false;
							break;
						}

					if(!cur->subtree_built && cur_subtree_built)
					{
						// If cur subtree was not built before, and now it is:

						if(areAllParentSubtreesBuilt(cur))
						{
							insertPendingMeshesForSubtree(cur);
						}

						cur->subtree_built = true;
					}

					if(!cur_subtree_built)
						break;

					cur = cur->parent;
				}
			}
		}

		checkInvariants();
	}
};



void processAllMessages(TerrainTestSystem& terrain)
{
	while(!terrain.tasks.empty())
	{
		Reference<MakeTestTerrainChunkTask> task = terrain.tasks.back();
		terrain.tasks.pop_back();

		TestTerrainChunkGeneratedMsg generated_msg;
		generated_msg.chunk_x = task->chunk_x;
		generated_msg.chunk_y = task->chunk_y;
		generated_msg.chunk_w = task->chunk_w;
		generated_msg.node_id = task->node_id;

		terrain.handleCompletedMakeChunkTask(generated_msg);
	}
}


void processSomeMessages(TerrainTestSystem& terrain, PCG32& rng)
{
	//std::random_device rd;
	//std::mt19937 generator(rd());

	//std::shuffle(terrain.tasks.begin(), terrain.tasks.end(), generator);
	if(terrain.tasks.size() > 0)
	{
		const int num = rng.nextUInt(terrain.tasks.size());
		for(int i=0; i<num; ++i)
		{
			const int task_i = rng.nextUInt(terrain.tasks.size());

			TestTerrainChunkGeneratedMsg generated_msg;
			generated_msg.chunk_x = terrain.tasks[task_i]->chunk_x;
			generated_msg.chunk_y = terrain.tasks[task_i]->chunk_y;
			generated_msg.chunk_w = terrain.tasks[task_i]->chunk_w;
			generated_msg.node_id = terrain.tasks[task_i]->node_id;

			mySwap(terrain.tasks[task_i], terrain.tasks[terrain.tasks.size() - 1]);

			terrain.tasks.pop_back();

			terrain.handleCompletedMakeChunkTask(generated_msg);
		}
	}
}


void checkNodeBuilt(TerrainTestSystem& terrain, TerrainTestNode* node)
{
	if(node->children[0].isNull()) // If leaf node:
	{
		testAssert(node->gl_ob.nonNull() && terrain.obs.count(node->gl_ob) > 0);

		// Check corresponding cells are correctly marked as being covered by this node's mesh
		for(int y=node->y; y<node->y + node->width; ++y)
		for(int x=node->x; x<node->x + node->width; ++x)
			testAssert(terrain.mesh_covering_square.elem(x, y) == node->id);
	}
	else // else interior:
	{
		for(int i=0; i<4; ++i)
			checkNodeBuilt(terrain, node->children[i].ptr());
	}
}


void checkAllLeafNodesBuilt(TerrainTestSystem& terrain)
{
	checkNodeBuilt(terrain, terrain.root_node.ptr());
}





static void printMeshCovering(TerrainTestSystem& terrain)
{
	for(int y=0; y<terrain.mesh_covering_square.getHeight(); ++y)
	{
		for(int x=0; x<terrain.mesh_covering_square.getWidth(); ++x)
		{
			uint64 val = terrain.mesh_covering_square.elem(x, y);
			conPrintStr((val == std::numeric_limits<uint64>::max() ? "e" : toString(val)) + " ");
		}
		conPrint("");
	}
}

void testTerrain()
{
	conPrint("testTerrain()");

//	{
//		TerrainTestSystem terrain(/*max_depth=*/1);
//
//		for(int i=0; i<4; ++i)
//		{
//			terrain.updateCampos(Vec3d(0,0,0));
//			testAssert(terrain.numNodes() == 5);
//
//			processMessages(terrain);
//
//			testAssert(terrain.areAllCellsFilled());
//
//			// Move cam far away, should merge nodes, so that root node = leaf
//			terrain.updateCampos(Vec3d(1.0e10f,0,0));
//			testAssert(terrain.numNodes() == 1);
//
//			testAssert(terrain.areAllCellsFilled());
//
//			processMessages(terrain);
//
//			testAssert(terrain.areAllCellsFilled());
//		}
//	}

//	{
//		TerrainTestSystem terrain(/*max_depth=*/2);
//
//		for(int i=0; i<4; ++i)
//		{
//			terrain.updateCampos(Vec3d(0,0,0));
//			testAssert(terrain.numNodes() == 1 + 4 + 16);
//
//			processMessages(terrain);
//
//			checkAllLeafNodesBuilt(terrain);
//
//			testAssert(terrain.areAllCellsFilled());
//
//			// Move cam far away, should merge nodes, so that root node = leaf
//			terrain.updateCampos(Vec3d(1.0e10f,0,0));
//			testAssert(terrain.numNodes() == 1);
//
//			terrain.updateCampos(Vec3d(0,0,0)); // Move back to origin immediately
//
//			terrain.updateCampos(Vec3d(1.0e10f,0,0)); // Move away immediately
//
//			testAssert(terrain.areAllCellsFilled());
//
//			processMessages(terrain);
//
//			checkAllLeafNodesBuilt(terrain);
//			testAssert(terrain.areAllCellsFilled());
//		}
//	}

	// Fuzz test
	{
		TerrainTestSystem terrain(/*max_depth=*/2);

		terrain.updateCampos(Vec3d(0,0,0));
		processAllMessages(terrain);
		testAssert(terrain.areAllCellsFilled());

		PCG32 rng(1);

		// i = 20184
		for(int i=0; i<100000000; ++i)
		{
			//if(i >= 20175)
			conPrint("--------------- i=" + toString(i) + "-----------------");
			{
				int a = 0;

				std::string s;
				terrain.appendSubtreeString(terrain.root_node.ptr(), s);
				conPrint(s);

				printMeshCovering(terrain);
			}

			if(rng.unitRandom() < 0.5) 
				terrain.updateCampos(Vec3d((rng.unitRandom() - 0.5f) * 4 * world_w, (rng.unitRandom() - 0.5f) * 4 * world_w, 0));
			else
			{
				if(rng.unitRandom() < 0.5) 
				{
					processAllMessages(terrain);
					checkAllLeafNodesBuilt(terrain);
				}
				else
				{
					processSomeMessages(terrain, rng);
				}
			}

			//testAssert(terrain.areAllCellsFilled());
			if(!terrain.areAllCellsFilled())
			{
				for(int y=0; y<terrain.mesh_covering_square.getHeight(); ++y)
				{
					for(int x=0; x<terrain.mesh_covering_square.getWidth(); ++x)
					{
						uint64 val = terrain.mesh_covering_square.elem(x, y);
						conPrintStr((val == std::numeric_limits<uint64>::max() ? "e" : toString(val)) + " ");
					}
					conPrint("");
				}
			}
		}
	}




//	{
//		TerrainTestSystem terrain(/*max_depth=*/4);
//
//		for(int i=0; i<4; ++i)
//		{
//			terrain.updateCampos(Vec3d(0,0,0));
//			//testAssert(terrain.numNodes() == 1 + 4 + 16 + 64);
//
//			processMessages(terrain);
//
//			testAssert(terrain.areAllCellsFilled());
//
//			// Move cam far away, should merge nodes, so that root node = leaf
//			terrain.updateCampos(Vec3d(1.0e10f,0,0));
//			testAssert(terrain.numNodes() == 1);
//
//			testAssert(terrain.areAllCellsFilled());
//
//			processMessages(terrain);
//
//			testAssert(terrain.areAllCellsFilled());
//		}
//	}

	conPrint("testTerrain() done.");
}

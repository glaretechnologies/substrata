/*=====================================================================
TerrainTests.h
--------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "IncludeOpenGL.h"
#include "FrameBuffer.h"
#include "OpenGLTexture.h"
#include "OpenGLEngine.h"
#include "PhysicsObject.h"
#include "../utils/RefCounted.h"
#include "../utils/Reference.h"
#include "../utils/Array2D.h"
#include "../maths/Matrix4f.h"
#include "../maths/vec3.h"
#include <string>
#include <map>
class OpenGLShader;
class OpenGLMeshRenderData;
class VertexBufferAllocator;


/*=====================================================================
TerrainTests
-------------
=====================================================================*/


void testTerrain();
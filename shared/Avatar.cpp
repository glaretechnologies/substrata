/*=====================================================================
Avatar.cpp
-------------------
Copyright Glare Technologies Limited 2016 -
Generated at 2016-01-12 12:24:54 +1300
=====================================================================*/
#include "Avatar.h"


#include "opengl/OpenGLEngine.h"


Avatar::Avatar()
{
	dirty = false;
	opengl_engine_ob = NULL;
	using_placeholder_model = false;
}


Avatar::~Avatar()
{

}




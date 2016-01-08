#include <GL/glew.h>


#include "GlWidget.h"


#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include "../indigo/globals.h"
#include "../graphics/Map2D.h"
#include "../graphics/imformatdecoder.h"
#include "../maths/vec3.h"
#include "../maths/GeometrySampling.h"
#include "../utils/Lock.h"
#include "../utils/Mutex.h"
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/Platform.h"
#include "../utils/FileUtils.h"
#include "../utils/Reference.h"
#include "../utils/StringUtils.h"
#include "../utils/CameraController.h"
#include "../utils/TaskManager.h"
#include <QtGui/QMouseEvent>
#include <set>
#include <stack>
#include <algorithm>


GlWidget::GlWidget(QWidget *parent)
:	QGLWidget(QGLFormat(QGL::SampleBuffers), parent),
	cam_controller(NULL),
	init_succeeded(false)
{
	viewport_aspect_ratio = 1;
}


GlWidget::~GlWidget()
{
}


static void 
#ifdef _WIN32
	// NOTE: not sure what this should be on non-windows platforms.  APIENTRY does not seem to be defined with GCC on Linux 64.
	APIENTRY 
#endif
myMessageCallback(
	GLenum _source, 
	GLenum _type, GLuint _id, GLenum _severity, 
	GLsizei _length, const char* _message, 
	void* _userParam) 
{
	// See https://www.opengl.org/sdk/docs/man/html/glDebugMessageControl.xhtml

	std::string type;
	switch(_type)
	{
	case GL_DEBUG_TYPE_ERROR: type = "Error"; break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type = "Deprecated Behaviour"; break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: type = "Undefined Behaviour"; break;
	case GL_DEBUG_TYPE_PORTABILITY: type = "Portability"; break;
	case GL_DEBUG_TYPE_PERFORMANCE: type = "Performance"; break;
	case GL_DEBUG_TYPE_MARKER: type = "Marker"; break;
	case GL_DEBUG_TYPE_PUSH_GROUP: type = "Push group"; break;
	case GL_DEBUG_TYPE_POP_GROUP: type = "Pop group"; break;
	case GL_DEBUG_TYPE_OTHER: type = "Other"; break;
	default: type = "Unknown"; break;
	}

	std::string severity;
	switch(_severity)
	{
	case GL_DEBUG_SEVERITY_LOW: severity = "low"; break;
	case GL_DEBUG_SEVERITY_MEDIUM: severity = "medium"; break;
	case GL_DEBUG_SEVERITY_HIGH : severity = "high"; break;
	case GL_DEBUG_SEVERITY_NOTIFICATION : severity = "notification"; break;
	case GL_DONT_CARE: severity = "Don't care"; break;
	default: severity = "Unknown"; break;
	}

	if(_severity != GL_DEBUG_SEVERITY_NOTIFICATION) // Don't print out notifications by default.
	{
		conPrint("==============================================================");
		conPrint("OpenGL msg, severity: " + severity + ", type: " + type + ":");
		conPrint(std::string(_message));
		conPrint("==============================================================");
	}
}


static void buildPassData(OpenGLPassData& pass_data, const std::vector<Vec3f>& vertices, const std::vector<Vec3f>& normals, const std::vector<Vec2f>& uvs)
{
	pass_data.vertices_vbo = new VBO(&vertices[0].x, vertices.size() * 3);

	if(!normals.empty())
		pass_data.normals_vbo = new VBO(&normals[0].x, normals.size() * 3);

	if(!uvs.empty())
		pass_data.uvs_vbo = new VBO(&uvs[0].x, uvs.size() * 2);
}


void GlWidget::initializeGL()
{
	GLenum err = glewInit();
	if(GLEW_OK != err)
	{
		conPrint("glewInit failed: " + std::string((const char*)glewGetErrorString(err)));
		init_succeeded = false;
		initialisation_error_msg = std::string((const char*)glewGetErrorString(err));
		return;
	}

	// conPrint("OpenGL version: " + std::string((const char*)glGetString(GL_VERSION)));

	// Check to see if OpenGL 2.0 is supported, which is required for our VBO usage.  (See https://www.opengl.org/sdk/docs/man/html/glGenBuffers.xhtml etc..)
	if(!GLEW_VERSION_2_0)
	{
		conPrint("OpenGL version is too old (< v2.0))");
		init_succeeded = false;
		initialisation_error_msg = "OpenGL version is too old (< v2.0))";
		return;
	}

#if BUILD_TESTS
	if(GLEW_ARB_debug_output)
	{
		// Enable error message handling,.
		// See "Porting Source to Linux: Valve’s Lessons Learned": https://developer.nvidia.com/sites/default/files/akamai/gamedev/docs/Porting%20Source%20to%20Linux.pdf
		glDebugMessageCallbackARB(myMessageCallback, NULL); 
		glEnable(GL_DEBUG_OUTPUT);
	}
	else
		conPrint("GLEW_ARB_debug_output OpenGL extension not available.");
#endif


	// Set up the rendering context, define display lists etc.:
	qglClearColor(QColor::fromRgb(32, 32, 32));

	glEnable(GL_NORMALIZE);		// Enable normalisation of normals
	glEnable(GL_DEPTH_TEST);	// Enable z-buffering
	glDisable(GL_CULL_FACE);	// Disable backface culling


	// Create VBO for unit AABB.
	{
		std::vector<Vec3f> corners;
		corners.push_back(Vec3f(0,0,0));
		corners.push_back(Vec3f(1,0,0));
		corners.push_back(Vec3f(0,1,0));
		corners.push_back(Vec3f(1,1,0));
		corners.push_back(Vec3f(0,0,1));
		corners.push_back(Vec3f(1,0,1));
		corners.push_back(Vec3f(0,1,1));
		corners.push_back(Vec3f(1,1,1));

		std::vector<Vec3f> verts;
		verts.push_back(corners[0]); verts.push_back(corners[1]); verts.push_back(corners[5]); verts.push_back(corners[4]); // front face
		verts.push_back(corners[4]); verts.push_back(corners[5]); verts.push_back(corners[7]); verts.push_back(corners[6]); // top face
		verts.push_back(corners[7]); verts.push_back(corners[5]); verts.push_back(corners[1]); verts.push_back(corners[3]); // right face
		verts.push_back(corners[6]); verts.push_back(corners[7]); verts.push_back(corners[3]); verts.push_back(corners[2]); // back face
		verts.push_back(corners[4]); verts.push_back(corners[6]); verts.push_back(corners[2]); verts.push_back(corners[0]); // left face
		verts.push_back(corners[0]); verts.push_back(corners[2]); verts.push_back(corners[3]); verts.push_back(corners[1]); // bottom face

		std::vector<Vec3f> normals;
		std::vector<Vec2f> uvs;

		aabb_passdata.num_prims = 6;
		aabb_passdata.material_index = 0;
		buildPassData(aabb_passdata, verts, normals, uvs);
	}

	init_succeeded = true;
}


void GlWidget::resizeGL(int width_, int height_)
{
	glViewport(0, 0, width_, height_);

	viewport_aspect_ratio = (double)width_ / (double)height_;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	swapBuffers();
}


void GlWidget::timeOutSlot()
{
	paintGL();
}


void GlWidget::paintGL()
{
	bool full_draw = false;
	draw(full_draw);
}


void GlWidget::draw(const bool high_quality)
{
	Timer profile_timer;

	if(!init_succeeded)
		return;

	// NOTE: We want to clear here first, even if the scene node is null.
	// Clearing here fixes the bug with the OpenGL widget buffer not being initialised properly and displaying garbled mem on OS X.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	const double render_aspect_ratio = 1;//(double)rs->getIntSetting("width") / (double)rs->getIntSetting("height");

	const double scene_scale = 100.0f;

	glLineWidth(1);

	// Initialise projection matrix from Indigo camera settings
	{
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();

		const double z_far  = scene_scale;
		const double z_near = scene_scale * 2e-5;

		
		double w, h;
		if(viewport_aspect_ratio > render_aspect_ratio)
		{
			h = 0.5 * 1/*cam->sensor_width / cam->lens_sensor_dist*/ / render_aspect_ratio;
			w = h * viewport_aspect_ratio;
		}
		else
		{
			w = 0.5 * 1/*cam->sensor_width / cam->lens_sensor_dist*/;
			h = w / viewport_aspect_ratio;
		}

		const double x_min = z_near * (-w);
		const double x_max = z_near * ( w);

		const double y_min = z_near * (-h);
		const double y_max = z_near * ( h);

		glFrustum(x_min, x_max, y_min, y_max, z_near, z_far);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		// Translate from Indigo to OpenGL camera view basis
		const float e[16] = { 1, 0, 0, 0,	0, 0, -1, 0,	0, 1, 0, 0,		0, 0, 0, 1 };
		glMultMatrixf(e);
	}

	glPushMatrix(); // Push camera transformation matrix

	// Do camera transformation
	Vec3d cam_pos(0, -1, 0);
	{
		Vec3d up, forwards, right;

	
		float e[16];

		// Apply camera rotation
		//getOpenGLViewRotation(forwards, up, e);
		//glMultMatrixf(e);

		// Apply camera translation
		getOpenGLViewTranslation(cam_pos, e);
		glMultMatrixf(e);
	}

	// Draw background env map if there is one.
	{
		if(env_mat.texture_loaded)
		{
			glPushMatrix();

			// Apply camera translation so that the background sphere is centered around the camera.
			glTranslatef(cam_pos.x, cam_pos.y, cam_pos.z);

			// Apply any rotation, mirroring etc. of the environment map.
			const float e[16]	= { (float)env_mat_transform.e[0], (float)env_mat_transform.e[3], (float)env_mat_transform.e[6], 0,
									(float)env_mat_transform.e[1], (float)env_mat_transform.e[4], (float)env_mat_transform.e[7], 0,
									(float)env_mat_transform.e[2], (float)env_mat_transform.e[5], (float)env_mat_transform.e[8], 0,	0, 0, 0, 1.f };
			glMultMatrixf(e);


			glDepthMask(GL_FALSE); // Disable writing to depth buffer.

			glDisable(GL_LIGHTING);
			glColor3f(1, 1, 1);
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

			drawPrimitives(env_mat, sphere_passdata, 4);
			
			glDepthMask(GL_TRUE); // Re-enable writing to depth buffer.

			glPopMatrix();
		}
	}


	// Set up lights
	{
		glEnable(GL_LIGHTING);
		glEnable(GL_LIGHT0); 

		const float ambient_amount = 0.5f;
		const float directional_amount = 0.5f;
		GLfloat light_ambient[]  = { ambient_amount, ambient_amount, ambient_amount, 1.0f };
		GLfloat light_diffuse[]  = { directional_amount, directional_amount, directional_amount, 1.0f };
		GLfloat light_specular[] = { directional_amount, directional_amount, directional_amount, 1.0f };
		GLfloat light_position[] = { (GLfloat)sun_dir.x,  (GLfloat)sun_dir.y,  (GLfloat)sun_dir.z, 0.0f };
		GLfloat light_spot_dir[] = { (GLfloat)-sun_dir.x, (GLfloat)-sun_dir.y, (GLfloat)-sun_dir.z, 0.0f };

		glLightfv(GL_LIGHT0, GL_AMBIENT,  light_ambient);
		glLightfv(GL_LIGHT0, GL_DIFFUSE,  light_diffuse);
		glLightfv(GL_LIGHT0, GL_SPECULAR, light_specular);
		glLightfv(GL_LIGHT0, GL_POSITION, light_position);
		glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, light_spot_dir);
	}


	glPopMatrix(); // Pop camera transformation matrix


	// Draw alpha-blended grey quads on the outside of 'render viewport'.
	// Also draw some blue lines on the edge of the quads.



	// Draw objects.

	for(size_t i=0; i<objects.size(); ++i)
	{
		const GLObject* ob = objects[i].getPointer();

		const GLRenderDataMap::const_iterator render_data_iter = mesh_render_data.find(ob->mesh.getPointer());

		const Reference<OpenGLRenderData>& render_data = render_data_iter->second;


		// Actual drawing begins here

		glPushMatrix();
		glTranslatef(ob->pos.x, ob->pos.y, ob->pos.z);
		glRotatef(::radToDegree(ob->rotation_angle), ob->rotation_axis.x, ob->rotation_axis.y, ob->rotation_axis.z);

		// Draw solid polygons
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		for(uint32 tri_pass = 0; tri_pass < render_data->tri_pass_data.size(); ++tri_pass)
		{
			const uint32 mat_index = myClamp<uint32>(tri_pass, 0, (uint32)ob->materials.size() - 1); //TEMP HACK mat tri pass stuff

			drawPrimitives(ob->materials[mat_index], render_data->tri_pass_data[tri_pass], 3);
		}

		for(uint32 quad_pass = 0; quad_pass < render_data->quad_pass_data.size(); ++quad_pass)
		{
			const uint32 mat_index = myClamp<uint32>(quad_pass, 0, (uint32)ob->materials.size() - 1); //TEMP HACK mat tri pass stuff

			drawPrimitives(ob->materials[mat_index], render_data->quad_pass_data[quad_pass], 4);
		}

		glPopMatrix();
	}

	swapBuffers(); // Block until drawing has been done and buffer has been displayed.

	//if(PROFILE) conPrint("IndigoGLWidget draw() (high_quality=" + boolToString(high_quality) + ") took " + profile_timer.elapsedStringNPlaces(4) + ", (" + doubleToStringNDecimalPlaces(1 / profile_timer.elapsed(), 2) + " fps)");
}


// KVP == (key, value) pair
class uint32KVP
{
public:
	inline bool operator()(const std::pair<uint32, uint32>& lhs, const std::pair<uint32, uint32>& rhs) const { return lhs.first < rhs.first; }
};


void GlWidget::buildMesh(const Indigo::Mesh& mesh_)
{
	const Indigo::Mesh* mesh = &mesh_;
	const bool mesh_has_shading_normals = !mesh->vert_normals.empty();
	const bool mesh_has_uvs = mesh->num_uv_mappings > 0;

	Reference<OpenGLRenderData> opengl_render_data = new OpenGLRenderData();

	// We will build up vertex data etc.. in these buffers, before the data is uploaded to a VBO.
	std::vector<Vec3f> vertices;
	std::vector<Vec3f> normals;
	std::vector<Vec2f> uvs;

	// Create per-material OpenGL triangle indices
	if(mesh->triangles.size() > 0)
	{
		// Create list of triangle references sorted by material index
		std::vector<std::pair<uint32, uint32> > tri_indices(mesh->triangles.size());
				
		assert(mesh->num_materials_referenced > 0);
		std::vector<uint32> material_tri_counts(mesh->num_materials_referenced, 0); // A count of how many triangles are using the given material i.
				
		for(uint32 t = 0; t < mesh->triangles.size(); ++t)
		{
			tri_indices[t] = std::make_pair(mesh->triangles[t].tri_mat_index, t);
			material_tri_counts[mesh->triangles[t].tri_mat_index]++;
		}
		std::sort(tri_indices.begin(), tri_indices.end(), uint32KVP());

		uint32 current_mat_index = std::numeric_limits<uint32>::max();
		OpenGLPassData* pass_data = NULL;
		uint32 offset = 0; // Offset into pass_data->vertices etc..

		for(uint32 t = 0; t < tri_indices.size(); ++t)
		{
			// If we've switched to a new material then start a new triangle range
			if(tri_indices[t].first != current_mat_index)
			{
				if(pass_data != NULL)
					buildPassData(*pass_data, vertices, normals, uvs); // Build last pass data

				current_mat_index = tri_indices[t].first;

				opengl_render_data->tri_pass_data.push_back(OpenGLPassData());
				pass_data = &opengl_render_data->tri_pass_data.back();

				pass_data->material_index = current_mat_index;
				pass_data->num_prims = material_tri_counts[current_mat_index];

				if(current_mat_index >= material_tri_counts.size())
				{
					assert(0);
					return;
				}

				// Resize arrays.
				vertices.resize(material_tri_counts[current_mat_index] * 3);
				normals.resize(material_tri_counts[current_mat_index] * 3);
				if(mesh_has_uvs)
					uvs.resize(material_tri_counts[current_mat_index] * 3);
				offset = 0;
			}


			const Indigo::Triangle& tri = mesh->triangles[tri_indices[t].second];


			// Return if vertex indices are bogus
			const size_t vert_positions_size = mesh->vert_positions.size();
			if(tri.vertex_indices[0] >= vert_positions_size) return;
			if(tri.vertex_indices[1] >= vert_positions_size) return;
			if(tri.vertex_indices[2] >= vert_positions_size) return;

			// Compute geometric normal if there are no shading normals in the mesh.
			Indigo::Vec3f N_g;
			if(!mesh_has_shading_normals)
			{
				Indigo::Vec3f v0 = mesh->vert_positions[tri.vertex_indices[0]];
				Indigo::Vec3f v1 = mesh->vert_positions[tri.vertex_indices[1]];
				Indigo::Vec3f v2 = mesh->vert_positions[tri.vertex_indices[2]];

				N_g = normalise(crossProduct(v1 - v0, v2 - v0));
			}


			for(uint32 i = 0; i < 3; ++i) // For each vert in tri:
			{
				const uint32 vert_i = tri.vertex_indices[i];

				const Indigo::Vec3f& v = mesh->vert_positions[vert_i];
				vertices[offset + i].set(v.x, v.y, v.z);

				if(!mesh_has_shading_normals)
				{
					normals[offset + i].set(N_g.x, N_g.y, N_g.z); // Use geometric normal
				}
				else
				{
					const Indigo::Vec3f& n = mesh->vert_normals[vert_i];
					normals[offset + i].set(n.x, n.y, n.z);
				}

				if(mesh_has_uvs)
				{
					const Indigo::Vec2f& uv = mesh->uv_pairs[tri.uv_indices[i]];
					uvs[offset + i].set(uv.x, uv.y);
				}
			}
			offset += 3;
		}

		// Build last pass data that won't have been built yet.
		if(pass_data != NULL)
			buildPassData(*pass_data, vertices, normals, uvs);
	}

	// Create per-material OpenGL quad indices
	if(mesh->quads.size() > 0)
	{
		// Create list of quad references sorted by material index
		std::vector<std::pair<uint32, uint32> > quad_indices(mesh->quads.size());

		assert(mesh->num_materials_referenced > 0);
		std::vector<uint32> material_quad_counts(mesh->num_materials_referenced, 0); // A count of how many quads are using the given material i.

		for(uint32 q = 0; q < mesh->quads.size(); ++q)
		{
			quad_indices[q] = std::make_pair(mesh->quads[q].mat_index, q);
			material_quad_counts[mesh->quads[q].mat_index]++;
		}
		std::sort(quad_indices.begin(), quad_indices.end(), uint32KVP());

		uint32 current_mat_index = std::numeric_limits<uint32>::max();
		OpenGLPassData* pass_data = NULL;
		uint32 offset = 0;

		for(uint32 q = 0; q < quad_indices.size(); ++q)
		{
			// If we've switched to a new material then start a new quad range
			if(quad_indices[q].first != current_mat_index)
			{
				if(pass_data != NULL)
					buildPassData(*pass_data, vertices, normals, uvs); // Build last pass data

				current_mat_index = quad_indices[q].first;

				opengl_render_data->quad_pass_data.push_back(OpenGLPassData());
				pass_data = &opengl_render_data->quad_pass_data.back();

				pass_data->material_index = current_mat_index;
				pass_data->num_prims = material_quad_counts[current_mat_index];

				if(current_mat_index >= material_quad_counts.size())
				{
					assert(0);
					return;
				}

				// Resize arrays.
				vertices.resize(material_quad_counts[current_mat_index] * 4);
				normals.resize(material_quad_counts[current_mat_index] * 4);
				if(mesh_has_uvs)
					uvs.resize(material_quad_counts[current_mat_index] * 4);
				offset = 0;
			}

					

			const Indigo::Quad& quad = mesh->quads[quad_indices[q].second];

			// Return if vertex indices are bogus
			const size_t vert_positions_size = mesh->vert_positions.size();
			if(quad.vertex_indices[0] >= vert_positions_size) return;
			if(quad.vertex_indices[1] >= vert_positions_size) return;
			if(quad.vertex_indices[2] >= vert_positions_size) return;
			if(quad.vertex_indices[3] >= vert_positions_size) return;

			// Compute geometric normal if there are no shading normals in the mesh.
			Indigo::Vec3f N_g;
			if(!mesh_has_shading_normals)
			{
				Indigo::Vec3f v0 = mesh->vert_positions[quad.vertex_indices[0]];
				Indigo::Vec3f v1 = mesh->vert_positions[quad.vertex_indices[1]];
				Indigo::Vec3f v2 = mesh->vert_positions[quad.vertex_indices[2]];

				N_g = normalise(crossProduct(v1 - v0, v2 - v0));
			}

			for(uint32 i = 0; i < 4; ++i)
			{
				const uint32 vert_i = quad.vertex_indices[i];

				const Indigo::Vec3f& v = mesh->vert_positions[vert_i];
				vertices[offset + i].set(v.x, v.y, v.z);

				if(mesh_has_shading_normals)
				{
					const Indigo::Vec3f& n = mesh->vert_normals[vert_i];
					normals[offset + i].set(n.x, n.y, n.z);
				}
				else
				{
					normals[offset + i].set(N_g.x, N_g.y, N_g.z); // Use geometric normal
				}

				if(mesh_has_uvs)
				{
					const Indigo::Vec2f& uv = mesh->uv_pairs[quad.uv_indices[i]];
					uvs[offset + i].set(uv.x, uv.y);
				}
			}
			offset += 4;
		}

		// Build last pass data that won't have been built yet.
		if(pass_data != NULL)
			buildPassData(*pass_data, vertices, normals, uvs);
	}

	// Add this node's info to the map
	mesh_render_data[mesh] = opengl_render_data;
}


void GlWidget::drawPrimitives(const OpenGLMaterial& opengl_mat, const OpenGLPassData& pass_data, int num_verts_per_primitive)
{
	assert(num_verts_per_primitive == 3 || num_verts_per_primitive == 4);

	if(opengl_mat.transparent)
		return; // Don't render transparent materials in the solid pass

	// Set OpenGL material state
	if(opengl_mat.albedo_texture_handle != 0 && pass_data.uvs_vbo.nonNull())
	{
		GLfloat mat_amb_diff[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_amb_diff);

		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, opengl_mat.albedo_texture_handle);

		// Set texture matrix
		glMatrixMode(GL_TEXTURE); // Enter texture matrix mode
		const GLfloat tex_elems[16] = {
			opengl_mat.tex_matrix.e[0], opengl_mat.tex_matrix.e[2], 0, 0,
			opengl_mat.tex_matrix.e[1], opengl_mat.tex_matrix.e[3], 0, 0,
			0, 0, 1, 0,
			opengl_mat.tex_translation.x, opengl_mat.tex_translation.y, 0, 1
		};
		glLoadMatrixf(tex_elems);
		glMatrixMode(GL_MODELVIEW); // Back to modelview mode

		glEnableClientState(GL_TEXTURE_COORD_ARRAY);							
		pass_data.uvs_vbo->bind();
		glTexCoordPointer(2, GL_FLOAT, 0, NULL);
		pass_data.uvs_vbo->unbind();
	}
	else
	{
		GLfloat mat_amb_diff[] = { opengl_mat.albedo_rgb[0], opengl_mat.albedo_rgb[1], opengl_mat.albedo_rgb[2], 1.0f };
		glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_amb_diff);
	}


	GLfloat mat_spec[] = { opengl_mat.specular_rgb[0], opengl_mat.specular_rgb[1], opengl_mat.specular_rgb[2], 1.0f };
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_spec);
	GLfloat shininess[] = { opengl_mat.shininess };
	glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, shininess);

	// Bind normals
	if(pass_data.normals_vbo.nonNull())
	{
		glEnableClientState(GL_NORMAL_ARRAY);
		pass_data.normals_vbo->bind();
		glNormalPointer(GL_FLOAT, 0, NULL);
		pass_data.normals_vbo->unbind();
	}

	glEnableClientState(GL_VERTEX_ARRAY);

	// Bind vertices
	pass_data.vertices_vbo->bind();
	glVertexPointer(3, GL_FLOAT, 0, NULL);
	pass_data.vertices_vbo->unbind();

	// Draw the triangles or quads
	if(num_verts_per_primitive == 3)
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)pass_data.num_prims * 3);
	else
		glDrawArrays(GL_QUADS, 0, (GLsizei)pass_data.num_prims * 4);

	glDisableClientState(GL_VERTEX_ARRAY);

	if(pass_data.normals_vbo.nonNull())
		glDisableClientState(GL_NORMAL_ARRAY);

	if(opengl_mat.albedo_texture_handle != 0)
	{
		glDisable(GL_TEXTURE_2D);

		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}
}


// glEnableClientState(GL_VERTEX_ARRAY); should be called before this function is called.
void GlWidget::drawPrimitivesWireframe(const OpenGLPassData& pass_data, int num_verts_per_primitive)
{
	assert(glIsEnabled(GL_VERTEX_ARRAY));

	// Bind vertices
	pass_data.vertices_vbo->bind();
	glVertexPointer(3, GL_FLOAT, 0, NULL);
	pass_data.vertices_vbo->unbind();

	// Draw the triangles or quads
	if(num_verts_per_primitive == 3)
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)pass_data.num_prims * 3);
	else
		glDrawArrays(GL_QUADS, 0, (GLsizei)pass_data.num_prims * 4);
}


void GlWidget::mousePressEvent(QMouseEvent* e)
{
	mouse_move_origin = QCursor::pos();
}


void GlWidget::showEvent(QShowEvent* e)
{
	emit widgetShowSignal();
}


void GlWidget::mouseMoveEvent(QMouseEvent* e)
{
	if(cam_controller != NULL && (e->modifiers() & Qt::AltModifier))
	{
		Qt::MouseButtons mb = e->buttons();
		double shift_scale = ((e->modifiers() & Qt::ShiftModifier) == 0) ? 1.0 : 0.35; // If shift is held, movement speed is roughly 1/3

		// Get new mouse position, movement vector and set previous mouse position to new.
		QPoint new_pos = QCursor::pos();
		QPoint delta(new_pos.x() - mouse_move_origin.x(),
					 mouse_move_origin.y() - new_pos.y()); // Y+ is down in screenspace, not up as desired.

		if(mb & Qt::LeftButton)  cam_controller->update(Vec3d(0, 0, 0), Vec2d(delta.y(), delta.x()) * shift_scale);
		if(mb & Qt::MidButton)   cam_controller->update(Vec3d(delta.x(), 0, delta.y()) * shift_scale, Vec2d(0, 0));
		if(mb & Qt::RightButton) cam_controller->update(Vec3d(0, delta.y(), 0) * shift_scale, Vec2d(0, 0));

		if(mb & Qt::RightButton || mb & Qt::LeftButton || mb & Qt::MidButton)
			emit cameraUpdated();

		mouse_move_origin = new_pos;
	}
}



void GlWidget::getOpenGLViewRotation(const Vec3d& forward_vector, const Vec3d& world_up_vector, float* matrix_out)
{
	const Vec3d right = normalise(crossProduct(forward_vector, world_up_vector));
	const Vec3d up = normalise(crossProduct(right, forward_vector));

	matrix_out[ 0] = (float)right.x;
	matrix_out[ 4] = (float)right.y;
	matrix_out[ 8] = (float)right.z;
	matrix_out[12] = 0;

	matrix_out[ 1] = (float)forward_vector.x;
	matrix_out[ 5] = (float)forward_vector.y;
	matrix_out[ 9] = (float)forward_vector.z;
	matrix_out[13] = 0;

	matrix_out[ 2] = (float)up.x;
	matrix_out[ 6] = (float)up.y;
	matrix_out[10] = (float)up.z;
	matrix_out[14] = 0;

	matrix_out[ 3] = 0;
	matrix_out[ 7] = 0;
	matrix_out[11] = 0;
	matrix_out[15] = 1;
}


void GlWidget::getOpenGLViewTranslation(const Vec3d& cam_pos, float* matrix_out)
{
	matrix_out[ 0] = 1;
	matrix_out[ 1] = 0;
	matrix_out[ 2] = 0;
	matrix_out[ 3] = 0;

	matrix_out[ 4] = 0;
	matrix_out[ 5] = 1;
	matrix_out[ 6] = 0;
	matrix_out[ 7] = 0;

	matrix_out[ 8] = 0;
	matrix_out[ 9] = 0;
	matrix_out[10] = 1;
	matrix_out[11] = 0;

	matrix_out[12] = (float)-cam_pos.x;
	matrix_out[13] = (float)-cam_pos.y;
	matrix_out[14] = (float)-cam_pos.z;
	matrix_out[15] = 1;
}

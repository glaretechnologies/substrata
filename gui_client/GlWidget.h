#pragma once


#include "../utils/IncludeWindows.h" // This needs to go first for NOMINMAX.
#include "../opengl/VBO.h"
#include "../maths/vec2.h"
#include "../maths/vec3.h"
#include "../maths/Matrix2.h"
#include "../maths/matrix3.h"
#include "../graphics/colour3.h"
#include "../utils/Timer.h"
#include "../utils/Reference.h"
#include "../utils/RefCounted.h"
#include <QtCore/QEvent>
#include <QtOpenGL/QGLWidget>
#include <map>


namespace Indigo { class Mesh; }
class CameraController;
class TextureServer;
class EnvEmitter;


// Data for a bunch of primitives from a given mesh, that all share the same material.
class OpenGLPassData
{
public:
	uint32 material_index;
	uint32 num_prims; // Number of triangles or quads.

	VBORef vertices_vbo;
	VBORef normals_vbo;
	VBORef uvs_vbo;
};


// OpenGL data for a given mesh.
class OpenGLRenderData : public RefCounted
{
public:
	std::vector<OpenGLPassData> tri_pass_data;
	std::vector<OpenGLPassData> quad_pass_data;
};


class OpenGLMaterial
{
public:
	OpenGLMaterial()
	:	transparent(false),
		texture_loaded(false),
		albedo_texture_handle(0),
		albedo_rgb(0.85f, 0.25f, 0.85f),
		specular_rgb(0.f),
		shininess(100.f),
		tex_matrix(1,0,0,1),
		tex_translation(0,0)
	{}

	Colour3f albedo_rgb; // First approximation to material colour
	Colour3f specular_rgb; // Used for OpenGL specular colour

	bool transparent;

	bool texture_loaded;
	GLuint albedo_texture_handle; // Handle to OpenGL-created texture on GPU

	Matrix2f tex_matrix;
	Vec2f tex_translation;

	float shininess;
};


struct GLObject : public RefCounted
{
	Reference<Indigo::Mesh> mesh; // Ref to a mesh.  Can be used to look up the OpenGLRenderData for the mesh.
	
	std::vector<OpenGLMaterial> materials;

	Vec3f pos;
	Vec3f rotation_axis;
	float rotation_angle;
};
typedef Reference<GLObject> GLObjectRef;


typedef std::map<const Indigo::Mesh*, Reference<OpenGLRenderData> > GLRenderDataMap;
//typedef std::map<Indigo::SceneNodeUID, OpenGLMaterial> GLMaterialMap;


class GlWidget : public QGLWidget
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	GlWidget(QWidget *parent = 0);
	~GlWidget();

	void initializeGL();
	void resizeGL(int w, int h);
	void paintGL(); // Does a "fast" draw, i.e. tries to maintain a reasonable frame rate (relatively low drawing timeout)

	void draw(const bool high_quality);

	void buildMesh(const Indigo::Mesh& mesh);

protected:
	void mousePressEvent(QMouseEvent* e);
	void mouseMoveEvent(QMouseEvent* e);

	void showEvent(QShowEvent* e);

signals:;
	void cameraUpdated();
	void widgetShowSignal();

protected slots:;
	virtual void timeOutSlot();
  
private:

	void buildPerspectiveProjMatrix(float* m, double FOV, double aspect_ratio, double z_near, double z_far, double x_max, double y_max);
	void drawPrimitives(const OpenGLMaterial& opengl_mat, const OpenGLPassData& pass_data, int num_verts_per_primitive);
	void drawPrimitivesWireframe(const OpenGLPassData& pass_data, int num_verts_per_primitive);
	void getOpenGLViewRotation(const Vec3d& forward_vector, const Vec3d& world_up_vector, float* matrix_out);
	void getOpenGLViewTranslation(const Vec3d& cam_pos, float* matrix_out);
public:
	bool init_succeeded;
	std::string initialisation_error_msg;
private:
	
	GLRenderDataMap mesh_render_data; // mesh node SceneNodeUID to tri and quad data
	//GLMaterialMap material_data; // material node SceneNodeUID to OpenGLMaterial.

	std::map<std::string, OpenGLMaterial> opengl_resized_textures;

	OpenGLMaterial env_mat;
	Matrix3f env_mat_transform;

	Vec3d sun_dir;

	QPoint mouse_prev_pos;
	QPoint mouse_move_origin;
	CameraController* cam_controller;

	std::string indigo_base_dir;

	OpenGLPassData aabb_passdata;
	OpenGLPassData sphere_passdata;

	float viewport_aspect_ratio;

public:
	std::vector<Reference<GLObject> > objects;
};

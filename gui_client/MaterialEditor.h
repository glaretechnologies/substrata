#pragma once


#include "../shared/WorldMaterial.h"
#include "../utils/IncludeWindows.h" // This needs to go first for NOMINMAX.
#include "../opengl/OpenGLEngine.h"
#include "../maths/vec2.h"
#include "../maths/vec3.h"
#include "../utils/Timer.h"
#include "../utils/Reference.h"
#include "../utils/RefCounted.h"
#include "ui_MaterialEditor.h"
#include <QtCore/QEvent>
#include <QtOpenGL/QGLWidget>
#include <map>


namespace Indigo { class Mesh; }
class TextureServer;
class EnvEmitter;


class MaterialEditor : public QWidget, public Ui::MaterialEditor
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	MaterialEditor(QWidget *parent = 0);
	~MaterialEditor();

	void setFromMaterial(const WorldMaterial& mat);
	void toMaterial(WorldMaterial& mat_out);

	void setControlsEnabled(bool enabled);

protected:

signals:;
	void materialChanged();
	
private:
	
};

#pragma once


#include "../shared/WorldObject.h"
#include "../utils/IncludeWindows.h" // This needs to go first for NOMINMAX.
#include "../opengl/OpenGLEngine.h"
#include "../maths/vec2.h"
#include "../maths/vec3.h"
#include "../utils/Timer.h"
#include "../utils/Reference.h"
#include "../utils/RefCounted.h"
#include "ui_ObjectEditor.h"
#include <QtCore/QEvent>
#include <QtOpenGL/QGLWidget>
#include <map>


namespace Indigo { class Mesh; }
class TextureServer;
class EnvEmitter;


class ObjectEditor : public QWidget, public Ui::ObjectEditor
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	ObjectEditor(QWidget *parent = 0);
	~ObjectEditor();

	void setFromObject(const WorldObject& mat);
	void toObject(WorldObject& mat_out);

	void setControlsEnabled(bool enabled);

protected:

signals:;
	void objectChanged();
	
private:
	
};

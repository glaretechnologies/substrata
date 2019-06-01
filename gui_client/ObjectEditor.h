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
class ShaderEditorDialog;


class ObjectEditor : public QWidget, public Ui::ObjectEditor
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	ObjectEditor(QWidget *parent = 0);
	~ObjectEditor();

	void setFromObject(const WorldObject& ob, int selected_mat_index);
	void updateObjectPos(const WorldObject& ob);
	void toObject(WorldObject& ob_out);

	void objectPickedUp();
	void objectDropped();

	void setControlsEnabled(bool enabled);

	void setControlsEditable(bool editable);

	int getSelectedMatIndex() const { return selected_mat_index; }

	void materialSelectedInBrowser(const std::string& path);

	std::string base_dir_path;
protected:

signals:;
	void objectChanged();
	
private slots:
	void on_visitURLLabel_linkActivated(const QString& link);
	void on_materialComboBox_currentIndexChanged(int index);
	void on_newMaterialPushButton_clicked(bool checked);
	void targetURLChanged();
	void scriptTextEditChanged();
	void scriptChangedFromEditor();
	void on_editScriptPushButton_clicked(bool checked);
	void editTimerTimeout();

private:
	// Store a cloned copy of the materials.
	// The reason for having this is so if the user selected another material,
	// we can display it, without needing to hang on to a reference to the original world object.
	std::vector<WorldMaterialRef> cloned_materials;

	int selected_mat_index;

	QTimer* edit_timer;

	ShaderEditorDialog* shader_editor;
};

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

	void init(); // settings should be set before this.

	void setFromObject(const WorldObject& ob, int selected_mat_index);
	void updateObjectPos(const WorldObject& ob);

	void toObject(WorldObject& ob_out);

	// Object details were updated from outside of the editor, for example due to an update message from the server.
	void objectModelURLUpdated(const WorldObject& ob);
	void objectLightmapURLUpdated(const WorldObject& ob);

	void objectPickedUp();
	void objectDropped();

	void setControlsEnabled(bool enabled);

	void setControlsEditable(bool editable);

	int getSelectedMatIndex() const { return selected_mat_index; }

	void materialSelectedInBrowser(const std::string& path);

	bool posAndRot3DControlsEnabled() { return show3DControlsCheckBox->isChecked(); }

	QSettings* settings;
	std::string base_dir_path;
protected:

signals:;
	void objectChanged();
	void bakeObjectLightmap();
	void bakeObjectLightmapHighQual();
	void removeLightmapSignal();
	void posAndRot3DControlsToggled();
	
private slots:
	void on_visitURLLabel_linkActivated(const QString& link);
	void on_materialComboBox_currentIndexChanged(int index);
	void on_newMaterialPushButton_clicked(bool checked);
	void targetURLChanged();
	void scriptTextEditChanged();
	void scriptChangedFromEditor();
	void on_editScriptPushButton_clicked(bool checked);
	void on_bakeLightmapPushButton_clicked(bool checked);
	void on_bakeLightmapHighQualPushButton_clicked(bool checked);
	void on_removeLightmapPushButton_clicked(bool checked);
	void editTimerTimeout();
	void xScaleChanged(double val);
	void yScaleChanged(double val);
	void zScaleChanged(double val);
	void linkScaleCheckBoxToggled(bool val);
	void on_spotlightColourPushButton_clicked(bool checked);

private:
	void updateSpotlightColourButton();
	// Store a cloned copy of the materials.
	// The reason for having this is so if the user selected another material,
	// we can display it, without needing to hang on to a reference to the original world object.
	std::vector<WorldMaterialRef> cloned_materials;

	int selected_mat_index;

	QTimer* edit_timer;

	ShaderEditorDialog* shader_editor;

	// Store the ratios between the scale components, used when link-scales is enabled.
	// Store ratios instead of individual values, as this allows us to preserve the rations if the scales are set to zero at some point.
	double last_x_scale_over_z_scale;
	double last_x_scale_over_y_scale;
	double last_y_scale_over_z_scale;

	Colour3f spotlight_col;
};

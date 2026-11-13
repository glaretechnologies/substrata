/*=====================================================================
ScriptEditorWindow.h
--------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "ui_ScriptEditorWindow.h"
#include "ISLSyntaxHighlighter.h"


class QTimer;
class QGridLayout;
class ResourceManager;


/*=====================================================================
ScriptEditorWindow
------------------
Window for editing an object's script.  There is one per object being edited, see MainWindow::openScriptEditorSlot().
=====================================================================*/
class ScriptEditorWindow : public QMainWindow, public Ui_ScriptEditorWindow
{
	Q_OBJECT
public:
	ScriptEditorWindow(QWidget* parent, std::string base_dir_path);
	virtual ~ScriptEditorWindow();

	void initialise(const std::string& shader);
	void update(const std::string& shader);

	QString getShaderText();

	void mouseDoubleClickedInOutput(ScriptEditorOutputTextEdit* sender, QMouseEvent* e);

	void printFromLuaScript(const std::string& msg);
	void luaErrorOccurred(const std::string& msg);

	void setReadOnly(bool read_only); // For when the user doesn't have permission to edit the object.

	void closeWithoutApplying(); // Closes the window without emitting shaderChanged for any unapplied edits.

	ResourceManager* resource_manager; // Used by 'Add file as Resource'.  May be null.
signals:;
	void shaderChanged();
	void openServerScriptLogSignal();

private:
	virtual void keyPressEvent(QKeyEvent* event);
	virtual void closeEvent(QCloseEvent* event);
	virtual void mouseDoubleClickEvent(QMouseEvent *e);

	QTimer* emit_shader_changed_timer;
	QTimer* syntax_highlight_timer;
	QTimer* flash_output_timer;
	ISLSyntaxHighlighter* highlighter;

private slots:;
	void on_shaderEdit_textChanged();
	void on_applyChangesPushButton_clicked();
	void on_addFileAsResourcePushButton_clicked();
	void emitShaderChangedTimerFired();
	void flashExecOutputTimerFired();
	void buildCodeAndShowResults();
	void shaderEditCursorPositionChanged();
	void on_openServerScriptLogLabel_linkActivated(const QString& link);

private:
	std::string base_dir_path;
	bool text_has_changed;

	struct SrcPosition
	{
		int line_num, char_num;
	};

	struct ErrorLocation
	{
		SrcPosition begin, end;
	};

	std::vector<ErrorLocation> error_locations;
};

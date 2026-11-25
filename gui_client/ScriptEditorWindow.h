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
class QLabel;
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
	void luaErrorOccurred(const std::string& msg, bool underline_error_line); // underline_error_line: Underline the script line given in msg, if the script hasn't been edited since it was applied.
	void luaWarningOccurred(const std::string& msg); // Shows msg in the output in orange.

	void setReadOnly(bool read_only); // For when the user doesn't have permission to edit the object.

	// Shows a notice, with an Unblock link, if the user has blocked on-screen items from the object or from all of its creator's objects.  Hides it if neither is blocked.
	void setScriptUIBlockedNotice(bool object_blocked, bool creator_blocked, const std::string& creator_name);

	void closeWithoutApplying(); // Closes the window without emitting shaderChanged for any unapplied edits.

	ResourceManager* resource_manager; // Used by 'Add file as Resource'.  May be null.
signals:;
	void shaderChanged();
	void openServerScriptLogSignal();
	void unblockScriptUISignal(); // The Unblock link in the blocked notice was clicked.

private:
	virtual void keyPressEvent(QKeyEvent* event);
	virtual void closeEvent(QCloseEvent* event);
	virtual void mouseDoubleClickEvent(QMouseEvent *e);

	QTimer* emit_shader_changed_timer;
	QTimer* syntax_highlight_timer;
	QTimer* flash_output_timer;
	ISLSyntaxHighlighter* highlighter;
	QLabel* script_ui_blocked_label; // See setScriptUIBlockedNotice().

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
	static bool parseLuaErrorLineNum(const std::string& msg, int& line_num_out);

	std::string base_dir_path;
	bool text_has_changed;
	bool text_edited_since_apply; // If true, the editor text may differ from the running script, so runtime error line numbers may not match it.

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

/*=====================================================================
ShaderEditorDialog.h
-------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "ui_ShaderEditorDialog.h"
#include "ISLSyntaxHighlighter.h"


class QTimer;
class QGridLayout;


/*=====================================================================
ShaderEditorDialog
-------------------

=====================================================================*/
class ShaderEditorDialog : public QMainWindow, public Ui_ShaderEditorDialog
{
	Q_OBJECT
public:
	ShaderEditorDialog(QWidget* parent, std::string base_dir_path);
	virtual ~ShaderEditorDialog();

	void initialise(const std::string& shader);
	void update(const std::string& shader);

	QString getShaderText();

	void mouseDoubleClickedInOutput(ShaderEditorOutputTextEdit* sender, QMouseEvent* e);

	void printFromLuaScript(const std::string& msg);
	void luaErrorOccurred(const std::string& msg);
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

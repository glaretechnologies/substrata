/*=====================================================================
ScriptEditorOutputTextEdit.h
----------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <QtWidgets/QTextEdit>
class ScriptEditorWindow;


/*=====================================================================
ScriptEditorOutputTextEdit
--------------------------
This class is just so we can override mouseDoubleClickEvent().
=====================================================================*/
class ScriptEditorOutputTextEdit : public QTextEdit
{
public:
	ScriptEditorOutputTextEdit(QWidget* parent);

	virtual void mouseDoubleClickEvent(QMouseEvent *e) override;

	ScriptEditorWindow* script_editor_window;
};

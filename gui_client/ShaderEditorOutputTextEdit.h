/*=====================================================================
ShaderEditorOutputTextEdit.h
----------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <QtWidgets/QTextEdit>
class ShaderEditorDialog;


/*=====================================================================
ShaderEditorOutputTextEdit
--------------------------
This class is just so we can override mouseDoubleClickEvent().
=====================================================================*/
class ShaderEditorOutputTextEdit : public QTextEdit
{
public:
	ShaderEditorOutputTextEdit(QWidget* parent);

	virtual void mouseDoubleClickEvent(QMouseEvent *e) override;

	ShaderEditorDialog* shader_editor_dialog;
};

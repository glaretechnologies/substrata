/*=====================================================================
ShaderEditorOutputTextEdit.cpp
------------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ShaderEditorOutputTextEdit.h"


#include "ShaderEditorDialog.h"
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>


ShaderEditorOutputTextEdit::ShaderEditorOutputTextEdit(QWidget* parent)
:	QTextEdit(parent),
	shader_editor_dialog(nullptr)
{
}


void ShaderEditorOutputTextEdit::mouseDoubleClickEvent(QMouseEvent* e)
{
	if(shader_editor_dialog)
		shader_editor_dialog->mouseDoubleClickedInOutput(this, e);
}

/*=====================================================================
ScriptEditorOutputTextEdit.cpp
------------------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "ScriptEditorOutputTextEdit.h"


#include "ScriptEditorWindow.h"
#include <utils/ConPrint.h>
#include <utils/StringUtils.h>


ScriptEditorOutputTextEdit::ScriptEditorOutputTextEdit(QWidget* parent)
:	QTextEdit(parent),
	script_editor_window(nullptr)
{
}


void ScriptEditorOutputTextEdit::mouseDoubleClickEvent(QMouseEvent* e)
{
	if(script_editor_window)
		script_editor_window->mouseDoubleClickedInOutput(this, e);
}

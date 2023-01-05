/*=====================================================================
ShaderEditorDialog.cpp
----------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "ShaderEditorDialog.h"


#include "WinterShaderEvaluator.h"
#include <QtCore/QTimer>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QColorDialog>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QMessageBox>
#include <QtCore/QSettings>
#include <qt/QtUtils.h>

#include "../../maths/mathstypes.h"
#include <string>
#include "../../utils/StringUtils.h"
#include "../../utils/Exception.h"
#include "../../utils/Timer.h"
#include "../../dll/IndigoStringUtils.h"


const int SHADER_EDIT_TIMEOUT = 300;
const int SYNTAX_HIGHLIGHT_TIMEOUT = 100;


ShaderEditorDialog::ShaderEditorDialog(QWidget* parent, std::string base_dir_path_)
:	QMainWindow(parent),
	base_dir_path(base_dir_path_)
{
	setupUi(this);

	QSettings settings("Glare Technologies", "Cyberspace");
	this->restoreGeometry(settings.value("shadereditor/geometry").toByteArray());

	emit_shader_changed_timer = new QTimer(this);
	connect(emit_shader_changed_timer, SIGNAL(timeout()), this, SLOT(emitShaderChangedTimerFired()));
	emit_shader_changed_timer->setSingleShot(true);

	syntax_highlight_timer = new QTimer(this);
	connect(syntax_highlight_timer, SIGNAL(timeout()), this, SLOT(buildCodeAndShowResults()));
	syntax_highlight_timer->setSingleShot(true);

	highlighter = new ISLSyntaxHighlighter(shaderEdit->document());

	int tab_size = this->shaderEdit->fontMetrics().horizontalAdvance("eval");
	this->shaderEdit->setTabStopDistance(tab_size);
}


ShaderEditorDialog::~ShaderEditorDialog()
{
}


void ShaderEditorDialog::initialise(const std::string& shader)
{
	this->blockSignals(true);

	shaderEdit->blockSignals(true);
	shaderEdit->setPlainText(QtUtils::toQString(shader));
	shaderEdit->blockSignals(false);

	this->blockSignals(false);

	buildCodeAndShowResults();
}


void ShaderEditorDialog::update(const std::string& shader)
{
	this->blockSignals(true);
	shaderEdit->blockSignals(true);
	shaderEdit->setPlainText(QtUtils::toQString(shader));
	shaderEdit->blockSignals(false);
	this->blockSignals(false);
}


QString ShaderEditorDialog::getShaderText()
{
	return shaderEdit->document()->toPlainText();
}


void ShaderEditorDialog::keyPressEvent(QKeyEvent* event)
{
	if(event->key() == Qt::Key_Escape)
	{
		this->close();
	}
	else QMainWindow::keyPressEvent(event);
}


void ShaderEditorDialog::closeEvent(QCloseEvent* event)
{
	QSettings settings("Glare Technologies", "Cyberspace");
	settings.setValue("shadereditor/geometry", saveGeometry());

	event->accept();
}


void ShaderEditorDialog::on_shaderEdit_textChanged()
{
	emit_shader_changed_timer->start(SHADER_EDIT_TIMEOUT);
	syntax_highlight_timer->start(SYNTAX_HIGHLIGHT_TIMEOUT);
}


void ShaderEditorDialog::emitShaderChangedTimerFired()
{
	emit shaderChanged();
}


void ShaderEditorDialog::buildCodeAndShowResults()
{
	const std::string shader = QtUtils::toIndString(shaderEdit->document()->toPlainText());
	try
	{
		Timer build_timer;

		Winter::VirtualMachineRef vm;
		WinterShaderEvaluator::EVAL_ROTATION_TYPE jitted_evalRotation;
		WinterShaderEvaluator::EVAL_TRANSLATION_TYPE jitted_evalTranslation;
		std::string error_msg;
		Winter::BufferPosition error_pos(NULL, 0, 0);

		WinterShaderEvaluator::build(base_dir_path, shader, vm, jitted_evalRotation, jitted_evalTranslation, error_msg, error_pos);

		const QSize status_label_size(60, 4);
		if(error_msg.empty())
		{
			this->outputTextEdit->setPlainText("Script built successfully."); // QtUtils::toQString("Script built successfully."));// in " + build_timer.elapsedString()));

			shaderEdit->blockSignals(true);
			this->highlighter->clearError();
			shaderEdit->blockSignals(false);

			QPixmap p(status_label_size);
			p.fill(Qt::green);
			this->buildStatusLabel->setPixmap(p);
		}
		else
		{
			this->outputTextEdit->setPlainText(QtUtils::toQString(error_msg)); // + Winter::Diagnostics::positionString(error_pos)));

			// Use error pos:
			shaderEdit->blockSignals(true);
			this->highlighter->showErrorAtCharIndex((int)error_pos.pos, (int)error_pos.len);
			shaderEdit->blockSignals(false);

			QPixmap p(status_label_size);
			p.fill(Qt::red);
			this->buildStatusLabel->setPixmap(p);
		}
	}
	catch(glare::Exception& e)
	{
		this->outputTextEdit->setPlainText(QtUtils::toQString(e.what()));
	}
}

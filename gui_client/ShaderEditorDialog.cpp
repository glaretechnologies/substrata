/*=====================================================================
ShaderEditorDialog.cpp
----------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "ShaderEditorDialog.h"


#include "WinterShaderEvaluator.h"
#include "Scripting.h"
#include "ObjectPathController.h"
#include "../shared/ResourceManager.h"
#include "../shared/FileTypes.h"
#include "../shared/LuaScriptEvaluator.h"
#include "../shared/WorldObject.h"
#include <QtCore/QTimer>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>
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
#include "../../utils/Parser.h"
#include "../../utils/FileUtils.h"
#include "../../dll/IndigoStringUtils.h"

#include <lua/LuaVM.h>
#include <lua/LuaScript.h>


const int SHADER_EDIT_TIMEOUT = 300;
const int SYNTAX_HIGHLIGHT_TIMEOUT = 100;


ShaderEditorDialog::ShaderEditorDialog(QWidget* parent, std::string base_dir_path_)
:	QMainWindow(parent),
	resource_manager(NULL),
	base_dir_path(base_dir_path_),
	text_has_changed(false)
{
	setupUi(this);

	this->buildOutputTextEdit    ->shader_editor_dialog = this;
	this->executionOutputTextEdit->shader_editor_dialog = this;

	QSettings settings("Glare Technologies", "Cyberspace");
	this->restoreGeometry(settings.value("shadereditor/geometry").toByteArray());

	emit_shader_changed_timer = new QTimer(this);
	connect(emit_shader_changed_timer, SIGNAL(timeout()), this, SLOT(emitShaderChangedTimerFired()));
	emit_shader_changed_timer->setSingleShot(true);

	syntax_highlight_timer = new QTimer(this);
	connect(syntax_highlight_timer, SIGNAL(timeout()), this, SLOT(buildCodeAndShowResults()));
	syntax_highlight_timer->setSingleShot(true);

	flash_output_timer = new QTimer(this);
	connect(flash_output_timer, SIGNAL(timeout()), this, SLOT(flashExecOutputTimerFired()));
	flash_output_timer->setSingleShot(true);

	connect(shaderEdit, SIGNAL(cursorPositionChanged()), this, SLOT(shaderEditCursorPositionChanged()));


	highlighter = new ISLSyntaxHighlighter(shaderEdit->document());

	int tab_size = this->shaderEdit->fontMetrics().horizontalAdvance("eval");
	this->shaderEdit->setTabStopDistance(tab_size);

	cursorPositionLabel->setText(QString());
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

	buildOutputTextEdit->clear();
	executionOutputTextEdit->clear();

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


void ShaderEditorDialog::mouseDoubleClickedInOutput(ShaderEditorOutputTextEdit* sender, QMouseEvent* e)
{
	if(sender == buildOutputTextEdit)
	{
		QTextCursor output_cursor = buildOutputTextEdit->cursorForPosition(e->pos());

		const int output_line_num = output_cursor.blockNumber();

		if(output_line_num >= 0 && output_line_num < (int)error_locations.size())
		{
			const ErrorLocation& loc = error_locations[output_line_num];

			// See https://stackoverflow.com/questions/40081248/qt-how-to-move-textedit-cursor-to-specific-col-and-row
			QTextCursor cursor = shaderEdit->textCursor();
	
			cursor.movePosition(QTextCursor::Start);
			cursor.movePosition(QTextCursor::Down,  QTextCursor::MoveAnchor, loc.begin.line_num);
			cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, loc.begin.char_num);
	
			shaderEdit->setTextCursor(cursor);

			shaderEdit->setFocus();
		}
	}
	else if(sender == executionOutputTextEdit)
	{
		QTextCursor cursor = executionOutputTextEdit->cursorForPosition(e->pos());

		const std::string line = QtUtils::toStdString(cursor.block().text());

		// Example runtime error messages:
		// Error while creating Lua script: [string "script"]:10: attempt to call a nil value
		// Lua, func: [top level code], line: 67
		// Lua, func: someFunc, line: 67

		
		std::string location_marker = "[string \"script\"]:";
		size_t offset = line.find(location_marker);
		if(offset == std::string::npos)
		{
			location_marker = ", line: ";
			offset = line.find(location_marker);
		}


		if(offset != std::string::npos)
		{
			const std::string trimmed_line = line.substr(offset + location_marker.size());
			Parser parser(trimmed_line);
			int line_num = 0;
			if(parser.parseInt(line_num))
			{
				// Move cursor to line where error occurred.
				QTextCursor cursor = shaderEdit->textCursor();

				cursor.movePosition(QTextCursor::Start);
				cursor.movePosition(QTextCursor::Down,  QTextCursor::MoveAnchor, line_num - 1);

				shaderEdit->setTextCursor(cursor);

				shaderEdit->setFocus();
			}
		}
	}
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
	if(text_has_changed)
		emit shaderChanged();

	QSettings settings("Glare Technologies", "Cyberspace");
	settings.setValue("shadereditor/geometry", saveGeometry());

	event->accept();
}


void ShaderEditorDialog::mouseDoubleClickEvent(QMouseEvent* e)
{

}


void ShaderEditorDialog::on_shaderEdit_textChanged()
{
	text_has_changed = true;
	emit_shader_changed_timer->start(SHADER_EDIT_TIMEOUT);
	syntax_highlight_timer->start(SYNTAX_HIGHLIGHT_TIMEOUT);
}


void ShaderEditorDialog::on_applyChangesPushButton_clicked()
{
	buildCodeAndShowResults();

	this->executionOutputTextEdit->clear();

	// Only show runtime output if the build succeeded
	if(!error_locations.empty()) // If there were build errors:
	{
		// Flash build output red
		QPalette p = buildOutputTextEdit->palette(); // Get current palette
		p.setColor(QPalette::Base, QColor(250, 200, 200)); // Set background colour
		buildOutputTextEdit->setPalette(p); // Update palette
	}
	else // else if no errors:
	{
		// Flash runtime output grey
		QPalette p = executionOutputTextEdit->palette(); // Get current palette
		p.setColor(QPalette::Base, QColor(220, 220, 220)); // Set background colour
		executionOutputTextEdit->setPalette(p); // Update palette
	}

	// Start timer to restore background colour
	const int FLASH_TIME_MS = 100;
	flash_output_timer->start(FLASH_TIME_MS);

	emit shaderChanged();
}


static void showResourceAddedDialog(QWidget* parent, const URLString& URL, const std::string& script_note)
{
	QDialog dialog(parent);
	dialog.setWindowTitle("Added file as Resource");

	QVBoxLayout* layout = new QVBoxLayout(&dialog);

	layout->addWidget(new QLabel("Resource URL:", &dialog));

	const QString URL_qstring = QtUtils::toQString(URL);

	QLineEdit* URL_line_edit = new QLineEdit(URL_qstring, &dialog);
	URL_line_edit->setReadOnly(true);
	URL_line_edit->setMinimumWidth(400);
	URL_line_edit->selectAll();
	layout->addWidget(URL_line_edit);

	QLabel* note_label = new QLabel(QtUtils::toQString(script_note), &dialog);
	note_label->setWordWrap(true);
	layout->addWidget(note_label);

	QHBoxLayout* button_layout = new QHBoxLayout();
	button_layout->addStretch();
	QPushButton* copy_button = new QPushButton("Copy URL", &dialog);
	QPushButton* close_button = new QPushButton("Close", &dialog);
	button_layout->addWidget(copy_button);
	button_layout->addWidget(close_button);
	layout->addLayout(button_layout);

	QObject::connect(copy_button, &QPushButton::clicked, &dialog, [URL_qstring]() { QApplication::clipboard()->setText(URL_qstring); });
	QObject::connect(close_button, &QPushButton::clicked, &dialog, &QDialog::accept);
	close_button->setDefault(true);

	dialog.exec();
}


void ShaderEditorDialog::on_addFileAsResourcePushButton_clicked()
{
	if(!resource_manager)
		return;

	QSettings settings("Glare Technologies", "Cyberspace");
	const QString last_dir = settings.value("shadereditor/lastAddFileAsResourceDir").toString();

	const QString selected_filename = QFileDialog::getOpenFileName(this,
		tr("Select file to add as resource..."),
		last_dir,
		tr("Images, models and audio (*.jpg *.jpeg *.png *.exr *.gif *.ktx *.ktx2 *.basis *.webp *.bmesh *.vox *.obj *.stl *.glb *.vrm *.igmesh *.subvox *.sog *.mp3 *.wav);;All files (*)")
	);
	if(selected_filename.isEmpty())
		return;

	const std::string path = QtUtils::toStdString(selected_filename);

	settings.setValue("shadereditor/lastAddFileAsResourceDir", QtUtils::toQString(FileUtils::getDirectory(path)));

	try
	{
		if(!FileTypes::hasSupportedExtension(path))
			throw glare::Exception("Unsupported file type: '" + FileUtils::getFilename(path) + "'");

		// Copy the file into the local resources dir.  It is uploaded to the server when the server requests it, which happens when an object that depends on it (for example by declaring it with @resource) is saved.
		const URLString URL = resource_manager->copyLocalFileToResourceDirAndReturnURL(path);

		std::string script = QtUtils::toIndString(shaderEdit->document()->toPlainText());

		DependencyURLVector declared_URLs;
		LuaScriptEvaluator::appendScriptResourceURLs(script, /*allocator=*/nullptr, declared_URLs);
		bool already_declared = false;
		for(size_t i=0; i<declared_URLs.size(); ++i)
			if(declared_URLs[i].URL == URL)
				already_declared = true;

		std::string script_note;
		if(already_declared)
		{
			script_note = "The script already declares this resource.";
		}
		else if(!LuaScriptEvaluator::addScriptResourceDeclaration(script, URL))
		{
			script_note = "The script is not a Lua script (it doesn't start with '--lua'), so the @resource line was not added.  "
				"The file will be uploaded once an object script declares it with an @resource line.";
		}
		else if(script.size() > WorldObject::MAX_SCRIPT_SIZE)
		{
			script_note = "Adding the @resource line would make the script too long, so it was not added.";
		}
		else
		{
			// Replace the text through a cursor, so the change can be undone in the editor.  This emits textChanged(), so the script is saved as for a manual edit.
			QTextCursor cursor(shaderEdit->document());
			cursor.select(QTextCursor::Document);
			cursor.insertText(QtUtils::toQString(script));

			script_note = "Added an @resource line for it to the script.  The file will be uploaded to the server when the script is applied (click Apply or close the editor).";
		}

		showResourceAddedDialog(this, URL, script_note);
	}
	catch(glare::Exception& e)
	{
		QtUtils::showErrorMessageDialog("Failed to add file as resource: " + e.what(), this);
	}
}


void ShaderEditorDialog::emitShaderChangedTimerFired()
{
	const std::string shader = QtUtils::toIndString(shaderEdit->document()->toPlainText());
	if(hasPrefix(shader, "--lua"))
	{
		// For Lua scripts, don't emit shaderChanged and hence reload script immediately, rather wait until the shader editor is closed.
	}
	else
		emit shaderChanged();
}


void ShaderEditorDialog::flashExecOutputTimerFired()
{
	// Restore output widget background colours to white
	{
		QPalette p = buildOutputTextEdit->palette(); // Get current palette
		p.setColor(QPalette::Base, Qt::white); // Set background colour
		buildOutputTextEdit->setPalette(p); // Update palette
	}
	{
		QPalette p = executionOutputTextEdit->palette(); // Get current palette
		p.setColor(QPalette::Base, Qt::white); // Set background colour
		executionOutputTextEdit->setPalette(p); // Update palette
	}
}


void ShaderEditorDialog::buildCodeAndShowResults()
{
	const QSize status_label_size(60, 4);

	this->error_locations.clear();

	const std::string shader = QtUtils::toIndString(shaderEdit->document()->toPlainText());
	if(hasPrefix(shader, "<?xml"))
	{
		buildResultsLabel->setText("Build Results (parsing as XML):");
		// Try and parse script as XML
		try
		{
			Reference<ObjectPathController> path_controller;
			Reference<Scripting::VehicleScript> vehicle_script;
			Scripting::parseXMLScript(NULL, shader, 0.0, path_controller, vehicle_script);


			this->buildOutputTextEdit->setPlainText("XML script built successfully.");
			this->executionOutputTextEdit->clear();

			shaderEdit->blockSignals(true); // Block signals so a text edited signal is not emitted when highlighting text.
			this->highlighter->clearError();
			this->highlighter->doRehighlight();
			shaderEdit->blockSignals(false);

			QPixmap p(status_label_size);
			p.fill(Qt::green);
			this->buildStatusLabel->setPixmap(p);
		}
		catch(glare::Exception& e)
		{
			this->buildOutputTextEdit->setPlainText(QtUtils::toQString(e.what())); // + Winter::Diagnostics::positionString(error_pos)));

			// Use error pos:
			shaderEdit->blockSignals(true);
			//this->highlighter->showErrorAtCharIndex((int)error_pos.pos, (int)error_pos.len);
			shaderEdit->blockSignals(false);

			QPixmap p(status_label_size);
			p.fill(Qt::red);
			this->buildStatusLabel->setPixmap(p);
		}
	}
	else if(hasPrefix(shader, "--lua"))
	{
		// Try and parse script as Lua

		buildResultsLabel->setText("Build Results (parsing as Lua):");
		highlighter->setCurLang(ISLSyntaxHighlighter::Lang_Lua);

		try
		{
			LuaVM lua_vm;
			lua_vm.finishInitAndSandbox();

			LuaScriptOptions options;
			options.max_num_interrupts = 100000;
			LuaScript lua_script(&lua_vm, options, /*script src=*/shader);

			this->buildOutputTextEdit->setPlainText("Lua script built successfully.");

			shaderEdit->blockSignals(true);
			this->highlighter->clearError();
			this->highlighter->doRehighlight();
			shaderEdit->blockSignals(false);

			QPixmap p(status_label_size);
			p.fill(Qt::green);
			this->buildStatusLabel->setPixmap(p);
		}
		catch(LuaScriptExcepWithLocation& e)
		{
			this->highlighter->clearError();

			std::string combined_msg;
			for(size_t i=0; i<e.errors.size(); ++i)
			{
				const Luau::Location location = e.errors[i].location; // With zero-based indices
				combined_msg += "Line " + toString(location.begin.line + 1) + ": " + e.errors[i].msg + "\n";
				
				try
				{
					const size_t begin_char_index = StringUtils::getCharIndexForLinePosition(shader, location.begin.line, location.begin.column);
					const size_t end_char_index   = StringUtils::getCharIndexForLinePosition(shader, location.end.line,   location.end.column);
					this->highlighter->addErrorAtCharIndex((int)begin_char_index, (int)(end_char_index - begin_char_index));

					ErrorLocation loc;
					loc.begin = SrcPosition({(int)location.begin.line, (int)location.begin.column});
					loc.end   = SrcPosition({(int)location.end.line,   (int)location.end.column});
					error_locations.push_back(loc);
				}
				catch(glare::Exception& ) // getCharIndexForLinePosition may throw
				{
					//assert(false);
				}
			}
			this->buildOutputTextEdit->setPlainText(QtUtils::toQString(combined_msg));

			shaderEdit->blockSignals(true); // Block signals so a text edited signal is not emitted when highlighting text.
			this->highlighter->doRehighlight();
			shaderEdit->blockSignals(false);

			QPixmap p(status_label_size);
			p.fill(Qt::red);
			this->buildStatusLabel->setPixmap(p);
		}
		catch(glare::Exception& e)
		{
			this->buildOutputTextEdit->setPlainText(QtUtils::toQString(e.what()));

			QPixmap p(status_label_size);
			p.fill(Qt::red);
			this->buildStatusLabel->setPixmap(p);
		}
	}
	else
	{
		// Try and parse script as Winter

		buildResultsLabel->setText("Build Results (parsing as Winter):");
		highlighter->setCurLang(ISLSyntaxHighlighter::Lang_Winter);

		try
		{
			Timer build_timer;

			Winter::VirtualMachineRef vm;
			WinterShaderEvaluator::EVAL_ROTATION_TYPE jitted_evalRotation;
			WinterShaderEvaluator::EVAL_TRANSLATION_TYPE jitted_evalTranslation;
			std::string error_msg;
			Winter::BufferPosition error_pos(NULL, 0, 0);

			WinterShaderEvaluator::build(base_dir_path, shader, vm, jitted_evalRotation, jitted_evalTranslation, error_msg, error_pos);

			if(error_msg.empty())
			{
				this->buildOutputTextEdit->setPlainText("Script built successfully."); // QtUtils::toQString("Script built successfully."));// in " + build_timer.elapsedString()));

				shaderEdit->blockSignals(true);
				this->highlighter->clearError();
				this->highlighter->doRehighlight();
				shaderEdit->blockSignals(false);

				QPixmap p(status_label_size);
				p.fill(Qt::green);
				this->buildStatusLabel->setPixmap(p);
			}
			else
			{
				this->buildOutputTextEdit->setPlainText(QtUtils::toQString(error_msg)); // + Winter::Diagnostics::positionString(error_pos)));

				// Use error pos:
				shaderEdit->blockSignals(true);
				this->highlighter->clearError();
				this->highlighter->addErrorAtCharIndex((int)error_pos.pos, (int)error_pos.len);
				this->highlighter->doRehighlight();
				shaderEdit->blockSignals(false);

				QPixmap p(status_label_size);
				p.fill(Qt::red);
				this->buildStatusLabel->setPixmap(p);
			}
		}
		catch(glare::Exception& e)
		{
			this->buildOutputTextEdit->setPlainText(QtUtils::toQString(e.what()));
		}
	}
}


void ShaderEditorDialog::shaderEditCursorPositionChanged()
{
	const int line_num = shaderEdit->textCursor().blockNumber() + 1;

	const int col_num = shaderEdit->textCursor().positionInBlock() + 1;

	cursorPositionLabel->setText(QtUtils::toQString("Line: " + toString(line_num) + ", Col: " + toString(col_num)));
}


void ShaderEditorDialog::on_openServerScriptLogLabel_linkActivated(const QString& link)
{
	// Emit a signal instead of opening the link directly, so that MainWindow can get the hostname we are connected to and use that in the URL.
	emit openServerScriptLogSignal();
}


void ShaderEditorDialog::printFromLuaScript(const std::string& msg)
{
	// Only show runtime output if the build succeeded
	if(error_locations.empty())
	{
		this->executionOutputTextEdit->moveCursor(QTextCursor::End);

		// Change text colour to black
		QTextCharFormat format = this->executionOutputTextEdit->currentCharFormat();
		format.setForeground(QBrush(QColor(0, 0, 0)));
		this->executionOutputTextEdit->setCurrentCharFormat(format);

		this->executionOutputTextEdit->insertPlainText(QtUtils::toQString(msg + "\n"));
	}
}


void ShaderEditorDialog::luaErrorOccurred(const std::string& msg)
{
	// Only show runtime errors if the build succeeded
	if(error_locations.empty())
	{
		this->executionOutputTextEdit->moveCursor(QTextCursor::End);

		// Change text colour to red
		QTextCharFormat format = this->executionOutputTextEdit->currentCharFormat();
		format.setForeground(QBrush(QColor(255, 0, 0)));
		this->executionOutputTextEdit->setCurrentCharFormat(format);

		this->executionOutputTextEdit->insertPlainText(QtUtils::toQString(msg + "\n"));
	}
}

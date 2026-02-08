/*=====================================================================
FileSelectWidget.cpp
-------------------
Copyright Glare Technologies Limited 2010 -
Generated at Thu Aug 30 15:22:06 +0200 2012
=====================================================================*/
#include "FileSelectWidget.h"


#include "qt/QtUtils.h"
#include "../../utils/FileUtils.h"
#include <QtWidgets/QFileDialog>
#include <assert.h>


FileSelectWidget::FileSelectWidget(QWidget* parent)
:	QWidget(parent),
	default_path(QDir::home().path()),
	settings_key("mainwindow/lastFileSelectDir"),
	type(Type_File),
	readonly(false),
	force_use_last_dir_setting(false)
{
	setupUi(this);

	internal_filter = tr("All files (*.*)");
}


FileSelectWidget::~FileSelectWidget()
{

}


void FileSelectWidget::setType(Type t)
{
	type = t;
}


const QString& FileSelectWidget::filename()
{
	return internal_filename;
}


void FileSelectWidget::setFilename(const QString& filename)
{
	// Make sure the QLineEdit doen't fire as well.
	internal_filename = filename;
	this->filePath->setText(internal_filename);
}


const QString& FileSelectWidget::filter()
{
	return internal_filter;
}


void FileSelectWidget::setFilter(const QString& filter)
{
	internal_filter = filter;
}


const QString& FileSelectWidget::defaultPath()
{
	return this->default_path;
}


void FileSelectWidget::setDefaultPath(const QString& path)
{
	this->default_path = path;
}


const QString& FileSelectWidget::settingsKey()
{
	return this->settings_key;
}


void FileSelectWidget::setSettingsKey(const QString& key)
{
	this->settings_key = key;
}


void FileSelectWidget::openFileDialog()
{
	QString previous_file = "";

	QSettings settings("Glare Technologies", "Cyberspace");

	// Check file exists when using as last path.  Fixes not opening to last dir for URLs. (that are not valid files)
	if(!this->filePath->text().isEmpty() && FileUtils::fileExists(QtUtils::toStdString(this->filePath->text())) && !force_use_last_dir_setting)
		previous_file = this->filePath->text();
	else //"mainwindow/lastEnvMapOpenedDir"
		previous_file = settings.value(settings_key, QVariant(default_path)).toString();

	QString file;
	if(type == Type_File)
		file = QFileDialog::getOpenFileName(this, "Select a file.", previous_file, internal_filter);
	else if(type == Type_File_Save)
		file = QFileDialog::getSaveFileName(this, "Select a file.", previous_file, internal_filter);
	else if (type == Type_Directory)
		file = QFileDialog::getExistingDirectory(this, "Select a directory.", previous_file);
	else
		assert(false);

	if(!file.isNull())
	{
		// Temp hack to make reselecting the same texture work in TextureControl2. Does not conform with the way qt works.
		//if(file != previous_file) // Don't do anything if it's the same.
		//{
			settings.setValue(settings_key, QtUtils::toQString(FileUtils::getDirectory(QtUtils::toIndString(file))));

			internal_filename = file;

			// Make sure the QLineEdit doesn't fire as well.
			this->filePath->blockSignals(true);
			this->filePath->setText(internal_filename);
			this->filePath->blockSignals(false);

			emit filenameChanged(internal_filename);
		//}
	}
}


void FileSelectWidget::setReadOnly(bool readonly_)
{
	readonly = readonly_;

	this->fileSelectButton->setEnabled(!readonly);
	this->filePath->setReadOnly(readonly);
}


void FileSelectWidget::on_fileSelectButton_clicked(bool v)
{
	openFileDialog();
}


void FileSelectWidget::on_filePath_editingFinished()
{
	QString new_file = filePath->text();

	if(readonly)
		return;

	if(type == Type_File_Save)
	{
		if(new_file != internal_filename) // If it actually changed.
		{
			internal_filename = new_file;

			emit filenameChanged(internal_filename);
		}
	}
	else // Directory or File Open mode. This will fail on files or directories that don't exist.
	{
		try
		{
			if(FileUtils::getCanonicalPath(QtUtils::toIndString(new_file)) != FileUtils::getCanonicalPath(QtUtils::toIndString(internal_filename))) // If it actually changed.
			{
				internal_filename = new_file;

				emit filenameChanged(internal_filename);
			}
		}
		catch(FileUtils::FileUtilsExcep&)
		{
			internal_filename = new_file;

			emit filenameChanged(internal_filename);
		}
	}
}

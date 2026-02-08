/*=====================================================================
FileSelectWidget.h
-------------------
Copyright Glare Technologies Limited 2010 -
Generated at Thu Aug 30 15:22:06 +0200 2012
=====================================================================*/
#pragma once


#include "ui_FileSelectWidget.h"

#include <QtCore/QSettings>


/*=====================================================================
FileSelectWidget
-------------------

=====================================================================*/
class FileSelectWidget : public QWidget, public Ui_FileSelectWidget
{
	Q_OBJECT
public:
	FileSelectWidget(QWidget* parent);
	~FileSelectWidget();

	enum Type
	{
		Type_File, // Pick an existing file (default)
		Type_File_Save, // Pick a file to be written
		Type_Directory // Pick an existing directory
	};

	void setType(Type t);

	const QString& filename();
	void setFilename(const QString& filename);

	const QString& filter();
	void setFilter(const QString& filter);

	const QString& defaultPath();
	void setDefaultPath(const QString& path);

	const QString& settingsKey();
	void setSettingsKey(const QString& key);


	void setReadOnly(bool readonly);

	bool force_use_last_dir_setting;

signals:;
	void filenameChanged(QString& filename);

public slots:;
	void openFileDialog();

private slots:;
	void on_fileSelectButton_clicked(bool v);
	void on_filePath_editingFinished();

private:
	QString internal_filename, internal_filter;
	QString default_path, settings_key;

	Type type;

	bool readonly;
};

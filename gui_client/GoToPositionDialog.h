/*=====================================================================
GoToPositionDialog.h
--------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "ui_GoToPositionDialog.h"
#include <QtCore/QString>
#include <maths/vec3.h>
class QSettings;


/*=====================================================================
GoToPositionDialog
------------------

=====================================================================*/
class GoToPositionDialog : public QDialog, public Ui_GoToPositionDialog
{
	Q_OBJECT
public:
	GoToPositionDialog(QSettings* settings, const Vec3d& current_pos);
	~GoToPositionDialog();

private:
	QSettings* settings;
};

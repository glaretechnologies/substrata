/*=====================================================================
ListObjectsNearbyDialog.h
-------------------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#pragma once


#include "ui_ListObjectsNearbyDialog.h"
#include "../shared/UID.h"
#include <maths/vec3.h>
#include <QtCore/QString>
class QSettings;
class WorldState;


/*=====================================================================
ListObjectsNearbyDialog
-----------------------

=====================================================================*/
class ListObjectsNearbyDialog : public QDialog, public Ui_ListObjectsNearbyDialog
{
	Q_OBJECT
public:
	ListObjectsNearbyDialog(QSettings* settings, WorldState* world_state, const Vec3d& cam_pos);
	~ListObjectsNearbyDialog();

	UID getSelectedUID() { return selected_uid; }

private slots:
	void itemSelectionChanged();

	void searchParametersChanged();

private:
	void updateResultsTable();

	WorldState* world_state;
	Vec3d cam_pos;

	QSettings* settings;

	UID selected_uid;
};

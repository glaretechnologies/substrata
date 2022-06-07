/*=====================================================================
ListObjectsNearbyDialog.cpp
---------------------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "ListObjectsNearbyDialog.h"


#include <QtCore/QSettings>
#include <QtWidgets/QTableWidget>
#include "../qt/QtUtils.h"
#include "WorldState.h"
#include <Lock.h>
#include <ConPrint.h>


ListObjectsNearbyDialog::ListObjectsNearbyDialog(QSettings* settings_, WorldState* world_state_, const Vec3d& cam_pos_)
:	settings(settings_),
	world_state(world_state_),
	cam_pos(cam_pos_)
{
	setupUi(this);

	objectTableWidget->setColumnCount(6);

	QStringList column_labels;
	column_labels.push_back("UID");
	column_labels.push_back("type");
	column_labels.push_back("Creator");
	column_labels.push_back("Creation time ago (hrs)");
	column_labels.push_back("model URL");
	column_labels.push_back("audio source URL");
	objectTableWidget->setHorizontalHeaderLabels(column_labels);

	objectTableWidget->verticalHeader()->setVisible(false); // Hide row numbers

	objectTableWidget->setColumnWidth(0, 80);
	objectTableWidget->setColumnWidth(1, 80);
	objectTableWidget->setColumnWidth(2, 100);
	objectTableWidget->setColumnWidth(3, 160);
	objectTableWidget->setColumnWidth(4, 400);
	objectTableWidget->setColumnWidth(5, 400);


	connect(this->objectTableWidget,			SIGNAL(itemSelectionChanged()),			this, SLOT(itemSelectionChanged()));
	connect(this->searchDistanceDoubleSpinBox,	SIGNAL(valueChanged(double)),			this, SLOT(searchParametersChanged()));
	connect(this->searchLineEdit,				SIGNAL(textChanged(const QString&)),	this, SLOT(searchParametersChanged()));

	// Remove question mark from the title bar (see https://stackoverflow.com/questions/81627/how-can-i-hide-delete-the-help-button-on-the-title-bar-of-a-qt-dialog)
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	// Load window geometry and state
	this->restoreGeometry(settings->value("ListObjectsNearbyDialog/geometry").toByteArray());

	// TODO: Save table state
	//this->objectTableWidget->restoreGeometry(settings->value("ListObjectsNearbyDialog/objectTableWidget_geometry").toByteArray());

	updateResultsTable();
}


ListObjectsNearbyDialog::~ListObjectsNearbyDialog()
{
	// Restore table state
	//settings->setValue("ListObjectsNearbyDialog/objectTableWidget_geometry", objectTableWidget->saveGeometry());

	settings->setValue("ListObjectsNearbyDialog/geometry", saveGeometry());
}


void ListObjectsNearbyDialog::itemSelectionChanged()
{
	const QList<QTableWidgetItem*> items = objectTableWidget->selectedItems();
	if(!items.empty())
	{
		const int row = items.front()->row();
		const QTableWidgetItem* uid_item = objectTableWidget->item(row, /*column=*/0);
		if(uid_item)
			this->selected_uid = UID(uid_item->text().toInt());
	}
}


void ListObjectsNearbyDialog::updateResultsTable()
{
	objectTableWidget->setSortingEnabled(false); // Disable sorting while updating table

	const double search_dist = this->searchDistanceDoubleSpinBox->value();

	const std::string search_term = QtUtils::toStdString(this->searchLineEdit->text());

	bool search_term_is_integer = false;
	int search_term_integer = 0;
	try
	{
		search_term_integer = stringToInt(search_term);
		search_term_is_integer = true;
	}
	catch(StringUtilsExcep& )
	{}


	{
		Lock lock(world_state->mutex);

		// Compute new num rows
		int num_rows = 0;
		for(auto it = world_state->objects.begin(); it != world_state->objects.end(); ++it)
		{
			const WorldObject* ob = it.getValuePtr();
			if(ob->pos.getDist(cam_pos) < search_dist)
			{
				if(search_term.empty() || 
					(search_term_is_integer && search_term_integer == (int)ob->uid.value()) || // If search term matches UID
					StringUtils::containsStringCaseInvariant(ob->creator_name, search_term) ||
					StringUtils::containsStringCaseInvariant(ob->model_url, search_term) ||
					StringUtils::containsStringCaseInvariant(ob->audio_source_url, search_term))
				{
					num_rows++;
				}
			}
		}
		objectTableWidget->setRowCount(num_rows);


		const TimeStamp now = TimeStamp::currentTime();

		int row = 0;
		for(auto it = world_state->objects.begin(); it != world_state->objects.end(); ++it)
		{
			const WorldObject* ob = it.getValuePtr();
			if(ob->pos.getDist(cam_pos) < search_dist)
			{
				if(search_term.empty() || 
					(search_term_is_integer && search_term_integer == (int)ob->uid.value()) || // If search term matches UID
					StringUtils::containsStringCaseInvariant(ob->creator_name, search_term) ||
					StringUtils::containsStringCaseInvariant(ob->model_url, search_term) ||
					StringUtils::containsStringCaseInvariant(ob->audio_source_url, search_term))
				{

					QTableWidgetItem* uid_item = new QTableWidgetItem(QtUtils::toQString(ob->uid.toString()));
					objectTableWidget->setItem(row, 0, uid_item);


					QTableWidgetItem* type_item = new QTableWidgetItem(QtUtils::toQString(ob->objectTypeString((WorldObject::ObjectType)ob->object_type)));
					objectTableWidget->setItem(row, 1, type_item);


					QTableWidgetItem* creator_name_item = new QTableWidgetItem(QtUtils::toQString(ob->creator_name));
					objectTableWidget->setItem(row,2, creator_name_item);


					const double secs_ago = now.time - ob->created_time.time;
					const double hrs_ago = secs_ago / 3600.0;
					std::string hrs_ago_string;
					if(hrs_ago < 8)
						hrs_ago_string = doubleToStringNDecimalPlaces(hrs_ago, 1);
					else
						hrs_ago_string = toString((int)hrs_ago);

					QTableWidgetItem* creation_time_ago_item = new QTableWidgetItem(QtUtils::toQString(hrs_ago_string));
					objectTableWidget->setItem(row,3, creation_time_ago_item);


					QTableWidgetItem* model_URL_item = new QTableWidgetItem(QtUtils::toQString(ob->model_url));
					objectTableWidget->setItem(row, 4, model_URL_item);


					QTableWidgetItem* audio_source_URL_item = new QTableWidgetItem(QtUtils::toQString(ob->audio_source_url));
					objectTableWidget->setItem(row, 5, audio_source_URL_item);

					row++;
				}
			}
		}
	} // End lock scope

	objectTableWidget->setSortingEnabled(true);
}


void ListObjectsNearbyDialog::searchParametersChanged()
{
	updateResultsTable();
}

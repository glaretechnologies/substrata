/*=====================================================================
WorldSettingsWidget.cpp
-----------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "WorldSettingsWidget.h"


#include "MainWindow.h"
#include "ClientThread.h"
#include "../qt/SignalBlocker.h"
#include "../shared/ResourceManager.h"
#include "../shared/Protocol.h"
#include "../shared/MessageUtils.h"
#include <qt/QtUtils.h>
#include <FileUtils.h>
#include <FileChecksum.h>
#include <QtCore/QSettings>
#include <QtWidgets/QMessageBox>


WorldSettingsWidget::WorldSettingsWidget(QWidget* parent)
:	QWidget(parent)
{
	setupUi(this);

	connect(this->newTerrainSectionPushButton, SIGNAL(clicked()), this, SLOT(newTerrainSectionPushButtonClicked()));

	//connect(this->applyPushButton, SIGNAL(clicked()), this, SIGNAL(settingsAppliedSignal()));
	connect(this->applyPushButton, SIGNAL(clicked()), this, SLOT(applySettingsSlot()));

	this->waterZDoubleSpinBox->setMinimum(-std::numeric_limits<double>::infinity());
	this->waterZDoubleSpinBox->setMaximum( std::numeric_limits<double>::infinity());

	this->defaultTerrainZDoubleSpinBox->setMinimum(-std::numeric_limits<double>::infinity());
	this->defaultTerrainZDoubleSpinBox->setMaximum( std::numeric_limits<double>::infinity());
}


WorldSettingsWidget::~WorldSettingsWidget()
{}


void WorldSettingsWidget::init(MainWindow* main_window_)
{
	main_window = main_window_;

	updateControlsEditable();
}


void WorldSettingsWidget::setFromWorldSettings(const WorldSettings& world_settings)
{
	QSignalBlocker blocker(this);

	QtUtils::ClearLayout(terrainSectionScrollAreaWidgetContents->layout(), /*delete widgets=*/true);

	for(size_t i=0; i<world_settings.terrain_spec.section_specs.size(); ++i)
	{
		const TerrainSpecSection& section_spec = world_settings.terrain_spec.section_specs[i];

		TerrainSpecSectionWidget* new_section_widget = new TerrainSpecSectionWidget(this);
		new_section_widget->xSpinBox->setValue(section_spec.x);
		new_section_widget->ySpinBox->setValue(section_spec.y);
		new_section_widget->heightmapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.heightmap_URL));
		new_section_widget->maskMapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.mask_map_URL));
		new_section_widget->treeMaskMapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.tree_mask_map_URL));

		const bool editable = main_window->connectedToUsersWorldOrGodUser();
		new_section_widget->updateControlsEditable(editable);

		terrainSectionScrollAreaWidgetContents->layout()->addWidget(new_section_widget);
		connect(new_section_widget, SIGNAL(removeButtonClickedSignal()), this, SLOT(removeTerrainSectionButtonClickedSlot()));
	}

	detailColMapURLs0FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[0]));
	detailColMapURLs1FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[1]));
	detailColMapURLs2FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[2]));
	detailColMapURLs3FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[3]));

	detailHeightMapURLs0FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_height_map_URLs[0]));

	terrainSectionWidthDoubleSpinBox->setValue(world_settings.terrain_spec.terrain_section_width_m);
	defaultTerrainZDoubleSpinBox->setValue(world_settings.terrain_spec.default_terrain_z);
	waterZDoubleSpinBox->setValue(world_settings.terrain_spec.water_z);
	waterCheckBox->setChecked(BitUtils::isBitSet(world_settings.terrain_spec.flags, TerrainSpec::WATER_ENABLED_FLAG));
}


URLString WorldSettingsWidget::getURLForFileSelectWidget(FileSelectWidget* widget)
{
	std::string current_URL_or_path = QtUtils::toStdString(widget->filename());

	// Copy all dependencies into resource directory if they are not there already.
	if(FileUtils::fileExists(current_URL_or_path)) // If this was a local path:
	{
		const std::string local_path = current_URL_or_path;
		const URLString URL = ResourceManager::URLForPathAndHash(local_path, FileChecksum::fileChecksum(local_path));

		// Copy model to local resources dir.
		main_window->gui_client.resource_manager->copyLocalFileToResourceDir(local_path, URL);

		return URL;
	}
	else
	{
		return toURLString(current_URL_or_path);
	}
}


void WorldSettingsWidget::toWorldSettings(WorldSettings& world_settings_out)
{
	world_settings_out.terrain_spec.section_specs.resize(0);
	for(int i=0; i<terrainSectionScrollAreaWidgetContents->layout()->count(); ++i)
	{
		QWidget* widget = terrainSectionScrollAreaWidgetContents->layout()->itemAt(i)->widget();
		TerrainSpecSectionWidget* section_widget = dynamic_cast<TerrainSpecSectionWidget*>(widget);
		if(section_widget)
		{
			TerrainSpecSection section;
			section.x = section_widget->xSpinBox->value();
			section.y = section_widget->ySpinBox->value();
			section.heightmap_URL = getURLForFileSelectWidget(section_widget->heightmapURLFileSelectWidget);
			section.mask_map_URL = getURLForFileSelectWidget(section_widget->maskMapURLFileSelectWidget);
			section.tree_mask_map_URL = getURLForFileSelectWidget(section_widget->treeMaskMapURLFileSelectWidget);

			world_settings_out.terrain_spec.section_specs.push_back(section);
		}
	}

	world_settings_out.terrain_spec.detail_col_map_URLs[0] = getURLForFileSelectWidget(detailColMapURLs0FileSelectWidget);
	world_settings_out.terrain_spec.detail_col_map_URLs[1] = getURLForFileSelectWidget(detailColMapURLs1FileSelectWidget);
	world_settings_out.terrain_spec.detail_col_map_URLs[2] = getURLForFileSelectWidget(detailColMapURLs2FileSelectWidget);
	world_settings_out.terrain_spec.detail_col_map_URLs[3] = getURLForFileSelectWidget(detailColMapURLs3FileSelectWidget);

	world_settings_out.terrain_spec.detail_height_map_URLs[0] = getURLForFileSelectWidget(detailHeightMapURLs0FileSelectWidget);

	world_settings_out.terrain_spec.terrain_section_width_m = (float)terrainSectionWidthDoubleSpinBox->value();
	world_settings_out.terrain_spec.default_terrain_z = (float)defaultTerrainZDoubleSpinBox->value();
	world_settings_out.terrain_spec.water_z = (float)waterZDoubleSpinBox->value();
	world_settings_out.terrain_spec.flags = (waterCheckBox->isChecked() ? TerrainSpec::WATER_ENABLED_FLAG : 0);
}


void WorldSettingsWidget::updateControlsEditable()
{
	const bool editable = main_window->connectedToUsersWorldOrGodUser();

	for(int i=0; i<terrainSectionScrollAreaWidgetContents->layout()->count(); ++i)
	{
		QWidget* widget = terrainSectionScrollAreaWidgetContents->layout()->itemAt(i)->widget();
		TerrainSpecSectionWidget* section_widget = dynamic_cast<TerrainSpecSectionWidget*>(widget);
		if(section_widget)
			section_widget->updateControlsEditable(editable);
	}

	newTerrainSectionPushButton->setEnabled(editable);

	detailColMapURLs0FileSelectWidget->setReadOnly(!editable);
	detailColMapURLs1FileSelectWidget->setReadOnly(!editable);
	detailColMapURLs2FileSelectWidget->setReadOnly(!editable);
	detailColMapURLs3FileSelectWidget->setReadOnly(!editable);
	
	detailHeightMapURLs0FileSelectWidget->setReadOnly(!editable);

	terrainSectionWidthDoubleSpinBox->setReadOnly(!editable);
	defaultTerrainZDoubleSpinBox->setReadOnly(!editable);
	waterZDoubleSpinBox->setReadOnly(!editable);
	waterCheckBox->setEnabled(editable);

	applyPushButton->setEnabled(editable);
}


void WorldSettingsWidget::newTerrainSectionPushButtonClicked()
{
	TerrainSpecSectionWidget* new_section_widget = new TerrainSpecSectionWidget(this);

	terrainSectionScrollAreaWidgetContents->layout()->addWidget(new_section_widget);

	connect(new_section_widget, SIGNAL(removeButtonClickedSignal()), this, SLOT(removeTerrainSectionButtonClickedSlot()));
}


void WorldSettingsWidget::removeTerrainSectionButtonClickedSlot()
{
	QObject* sender_ob = QObject::sender();

	terrainSectionScrollAreaWidgetContents->layout()->removeWidget((QWidget*)sender_ob);

	sender_ob->deleteLater();
}


static void enqueueMessageToSend(ClientThread& client_thread, SocketBufferOutStream& packet)
{
	MessageUtils::updatePacketLengthField(packet);

	client_thread.enqueueDataToSend(packet.buf);
}


void WorldSettingsWidget::applySettingsSlot()
{
	try
	{
		SocketBufferOutStream scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder);
		MessageUtils::initPacket(scratch_packet, Protocol::WorldSettingsUpdate);
	
		this->toWorldSettings(main_window->gui_client.connected_world_settings);

		main_window->gui_client.connected_world_settings.writeToStream(scratch_packet);

		enqueueMessageToSend(*main_window->gui_client.client_thread, scratch_packet);

		emit settingsAppliedSignal();
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}

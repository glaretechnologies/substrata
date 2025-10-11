#include "ParcelEditor.h"


#include "../qt/SignalBlocker.h"
#include "../qt/QtUtils.h"
#include <QtGui/QDesktopServices>


ParcelEditor::ParcelEditor(QWidget *parent)
:	QWidget(parent)
{
	setupUi(this);

	connect(this->descriptionTextEdit,			SIGNAL(textChanged()),				this, SIGNAL(parcelChanged()));
	connect(this->allWriteableCheckBox,			SIGNAL(toggled(bool)),				this, SIGNAL(parcelChanged()));
	connect(this->muteOutsideAudioCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(parcelChanged()));
	connect(this->spawnXDoubleSpinBox,			SIGNAL(valueChanged(double)),		this, SIGNAL(parcelChanged()));
	connect(this->spawnYDoubleSpinBox,			SIGNAL(valueChanged(double)),		this, SIGNAL(parcelChanged()));
	connect(this->spawnZDoubleSpinBox,			SIGNAL(valueChanged(double)),		this, SIGNAL(parcelChanged()));
}


ParcelEditor::~ParcelEditor()
{
}


void ParcelEditor::setFromParcel(const Parcel& parcel)
{
	this->IDLabel->setText(QtUtils::toQString(parcel.id.toString()));

	const std::string owner_name = !parcel.owner_name.empty() ? parcel.owner_name :
		(parcel.owner_id.valid() ? ("user id: " + parcel.owner_id.toString()) : "[Unknown]");

	this->ownerLabel->setText(QtUtils::toQString(owner_name));
	this->createdTimeLabel->setText(QtUtils::toQString(parcel.created_time.timeAgoDescription()));

	{
		SignalBlocker b(this->descriptionTextEdit);
		this->descriptionTextEdit->setPlainText(QtUtils::toQString(parcel.description));
	}
	{
		SignalBlocker b(this->writersTextEdit);
		this->writersTextEdit->setPlainText(QtUtils::toQString(StringUtils::join(parcel.writer_names, ",\n")));
	}
	{
		SignalBlocker b(this->adminsTextEdit);
		this->adminsTextEdit->setPlainText(QtUtils::toQString(StringUtils::join(parcel.admin_names, ",\n")));
	}

	SignalBlocker::setChecked(this->allWriteableCheckBox, parcel.all_writeable);

	SignalBlocker::setChecked(this->muteOutsideAudioCheckBox, BitUtils::isBitSet(parcel.flags, Parcel::MUTE_OUTSIDE_AUDIO_FLAG));

	this->minLabel->setText(QtUtils::toQString(parcel.aabb_min.toString()));
	this->maxLabel->setText(QtUtils::toQString(parcel.aabb_max.toString()));

	SignalBlocker::setValue(this->spawnXDoubleSpinBox, parcel.spawn_point.x);
	SignalBlocker::setValue(this->spawnYDoubleSpinBox, parcel.spawn_point.y);
	SignalBlocker::setValue(this->spawnZDoubleSpinBox, parcel.spawn_point.z);
}


void ParcelEditor::toParcel(Parcel& parcel_out)
{
	parcel_out.description = QtUtils::toStdString(this->descriptionTextEdit->toPlainText());

	parcel_out.all_writeable = this->allWriteableCheckBox->isChecked();

	const bool mute_outside_audio = this->muteOutsideAudioCheckBox->isChecked();
	parcel_out.flags = 0;
	if(mute_outside_audio)
		BitUtils::setBit(parcel_out.flags, Parcel::MUTE_OUTSIDE_AUDIO_FLAG);

	parcel_out.spawn_point.x = this->spawnXDoubleSpinBox->value();
	parcel_out.spawn_point.y = this->spawnYDoubleSpinBox->value();
	parcel_out.spawn_point.z = this->spawnZDoubleSpinBox->value();
}


void ParcelEditor::setCurrentServerURL(const std::string& server_url)
{
	current_server_url = server_url;
	
	// Update the link text dynamically based on current server
	std::string hostname = "substrata.info"; // Default fallback
	
	if(!current_server_url.empty())
	{
		// Parse URL to extract hostname
		// Format: sub://hostname:port/path
		if(current_server_url.find("sub://") == 0)
		{
			std::string url_part = current_server_url.substr(6); // Remove "sub://"
			size_t colon_pos = url_part.find(':');
			size_t slash_pos = url_part.find('/');
			
			if(colon_pos != std::string::npos)
				hostname = url_part.substr(0, colon_pos);
			else if(slash_pos != std::string::npos)
				hostname = url_part.substr(0, slash_pos);
			else
				hostname = url_part;
		}
	}
	
	// Update the link text
	QString link_text = QString::fromStdString("<a href=\"#boo\">Show parcel on " + hostname + "</a>");
	this->showOnWebLabel->setText(link_text);
}

void ParcelEditor::on_showOnWebLabel_linkActivated(const QString&)
{
	// Extract hostname from server URL
	std::string hostname = "substrata.info"; // Default fallback
	
	if(!current_server_url.empty())
	{
		// Parse URL to extract hostname
		// Format: sub://hostname:port/path
		if(current_server_url.find("sub://") == 0)
		{
			std::string url_part = current_server_url.substr(6); // Remove "sub://"
			size_t colon_pos = url_part.find(':');
			size_t slash_pos = url_part.find('/');
			
			if(colon_pos != std::string::npos)
				hostname = url_part.substr(0, colon_pos);
			else if(slash_pos != std::string::npos)
				hostname = url_part.substr(0, slash_pos);
			else
				hostname = url_part;
		}
	}
	
	QDesktopServices::openUrl(QString::fromStdString("https://" + hostname + "/parcel/" + this->IDLabel->text().toStdString()));
}

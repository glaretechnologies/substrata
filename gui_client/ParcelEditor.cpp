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
}


void ParcelEditor::toParcel(Parcel& parcel_out)
{
	parcel_out.description = QtUtils::toStdString(this->descriptionTextEdit->toPlainText());

	parcel_out.all_writeable = this->allWriteableCheckBox->isChecked();

	const bool mute_outside_audio = this->muteOutsideAudioCheckBox->isChecked();
	parcel_out.flags = 0;
	if(mute_outside_audio)
		BitUtils::setBit(parcel_out.flags, Parcel::MUTE_OUTSIDE_AUDIO_FLAG);
}


void ParcelEditor::on_showOnWebLabel_linkActivated(const QString&)
{
	QDesktopServices::openUrl("https://substrata.info/parcel/" + this->IDLabel->text());
}

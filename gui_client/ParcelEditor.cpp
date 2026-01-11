#include "ParcelEditor.h"


#include "../qt/SignalBlocker.h"
#include "../qt/QtUtils.h"
#include <QtGui/QDesktopServices>


ParcelEditor::ParcelEditor(QWidget *parent)
:	QWidget(parent)
{
	setupUi(this);
	
	connect(this->titleLineEdit,				SIGNAL(textChanged(const QString&)),this, SIGNAL(parcelChanged()));
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
		SignalBlocker b(this->titleLineEdit);
		this->titleLineEdit->setText(QtUtils::toQString(parcel.title));
	}
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


template <class StringType>
static void checkStringSize(StringType& s, size_t max_size)
{
	// TODO: throw exception instead?
	if(s.size() > max_size)
		s = s.substr(0, max_size);
}


void ParcelEditor::toParcel(Parcel& parcel_out)
{
	parcel_out.title = QtUtils::toStdString(this->titleLineEdit->text());
	checkStringSize(parcel_out.title, Parcel::MAX_TITLE_SIZE);

	parcel_out.description = QtUtils::toStdString(this->descriptionTextEdit->toPlainText());
	checkStringSize(parcel_out.description, Parcel::MAX_DESCRIPTION_SIZE);

	parcel_out.all_writeable = this->allWriteableCheckBox->isChecked();

	const bool mute_outside_audio = this->muteOutsideAudioCheckBox->isChecked();
	parcel_out.flags = 0;
	if(mute_outside_audio)
		BitUtils::setBit(parcel_out.flags, Parcel::MUTE_OUTSIDE_AUDIO_FLAG);

	parcel_out.spawn_point.x = this->spawnXDoubleSpinBox->value();
	parcel_out.spawn_point.y = this->spawnYDoubleSpinBox->value();
	parcel_out.spawn_point.z = this->spawnZDoubleSpinBox->value();
}


void ParcelEditor::on_showOnWebLabel_linkActivated(const QString&)
{
	QDesktopServices::openUrl("https://substrata.info/parcel/" + this->IDLabel->text());
}

#include "ParcelEditor.h"


#include "../qt/SignalBlocker.h"
#include "../qt/QtUtils.h"


ParcelEditor::ParcelEditor(QWidget *parent)
:	QWidget(parent)
{
	setupUi(this);
	this->allWriteableCheckBox->setEnabled(false); // Do this as the most similar to read-only.
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
		this->descriptionTextEdit->setText(QtUtils::toQString(parcel.description));
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

	this->minLabel->setText(QtUtils::toQString(parcel.aabb_min.toString()));
	this->maxLabel->setText(QtUtils::toQString(parcel.aabb_max.toString()));
}

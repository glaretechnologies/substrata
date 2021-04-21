#pragma once


#include "../shared/Parcel.h"
#include "ui_ParcelEditor.h"


class ParcelEditor : public QWidget, public Ui::ParcelEditor
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	ParcelEditor(QWidget *parent = 0);
	~ParcelEditor();

	void setFromParcel(const Parcel& parcel);

private slots:
	void on_showOnWebLabel_linkActivated(const QString& link);

protected:
};

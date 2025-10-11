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

	void toParcel(Parcel& parcel_out);

	void setCurrentServerURL(const std::string& server_url);

signals:;
	void parcelChanged();

private slots:
	void on_showOnWebLabel_linkActivated(const QString& link);

protected:
	std::string current_server_url;
};

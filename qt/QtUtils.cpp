/*=====================================================================
QtUtils.cpp
-----------
File created by ClassTemplate on Mon Jun 29 15:03:47 2009
Code By Nicholas Chapman.
=====================================================================*/
#include "QtUtils.h"


#include <QtWidgets/QErrorMessage>
#include <QtGui/QTextDocument> // for Qt::escape()
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLayout>
#include <QtCore/QUrl>
#include "../utils/ArgumentParser.h"
#include "../utils/PlatformUtils.h"
#include <limits>
#include <assert.h>
#include "../dll/include/IndigoString.h"


namespace QtUtils
{

/*
	Convert an Indigo string into a QT string.
*/
const QString toQString(const std::string& s)
{
	assert(s.length() < (size_t)std::numeric_limits<int>::max());

	return QString::fromUtf8(
		s.c_str(), 
		(int)s.length() // num bytes
	);
}


const QString toQString(const Indigo::String& s)
{
	assert(s.length() < (size_t)std::numeric_limits<int>::max());

	return QString::fromUtf8(
		s.dataPtr(), 
		(int)s.length() // num bytes
	);
}



/*
	Convert a QT string to an std string.
*/
const std::string toIndString(const QString& s)
{
	const QByteArray bytes = s.toUtf8();

	return std::string(bytes.constData(), bytes.size());
}


/*
	Convert a QT string to an Indigo::String.
*/
const Indigo::String toIndigoString(const QString& s)
{
	const QByteArray bytes = s.toUtf8();

	return Indigo::String(bytes.constData(), bytes.size());
}


const QString imageLoadFilters()
{
	return QString("Images (*.png *.jpg *.jpeg *.gif *.tif *.tiff *.exr *.hdr);;\
				   Portable Network Graphics (*.png);;Jpeg (*.jpg *.jpeg);;Truevision Targa (*.tga);;\
				   Graphics Interchange Format (*.gif);;Tagged Image File Format (*.tiff *.tif);;\
				   OpenEXR (*.exr);;RGBE (*.hdr)");
}


void showErrorMessageDialog(const std::string& error_msg, QWidget* parent)
{
	QErrorMessage m(parent);
	m.setWindowTitle(QObject::tr("Error"));
	m.showMessage((QObject::tr("Error: ") + QtUtils::toQString(error_msg)).toHtmlEscaped());
	m.exec();
}


void showErrorMessageDialog(const QString& error_msg, QWidget* parent)
{
	QErrorMessage m(parent);
	m.setWindowTitle(QObject::tr("Error"));
	m.showMessage((QObject::tr("Error: ") + error_msg).toHtmlEscaped());
	m.exec();
}


/*
backgroundModeCheckBox may be NULL.
*/
void setProcessPriority(const ArgumentParser& parsed_args, QCheckBox* backgroundModeCheckBox)
{
	if(parsed_args.isArgPresent("--thread_priority"))
	{
		if(parsed_args.getArgStringValue("--thread_priority") == "normal")
		{
			if(backgroundModeCheckBox)
				backgroundModeCheckBox->setChecked(false);
		}
		else if(parsed_args.getArgStringValue("--thread_priority") == "belownormal")
		{
			try
			{
				PlatformUtils::setThisProcessPriority(PlatformUtils::BelowNormal_Priority);
			}
			catch(PlatformUtils::PlatformUtilsExcep&)
			{}

			if(backgroundModeCheckBox)
				backgroundModeCheckBox->setChecked(true);
		}
	}
	else
	{
		// if arg not present, just set to below normal priority by defailt.
		try
		{
			PlatformUtils::setThisProcessPriority(PlatformUtils::BelowNormal_Priority);
		}
		catch(PlatformUtils::PlatformUtilsExcep&)
		{}

		if(backgroundModeCheckBox)
			backgroundModeCheckBox->setChecked(true);
	}
}


/* 
 * Removes all LayoutItems (which hold Widgets) from a layout,
 * deletes the Items AND their widgets - be careful!
 * when deleteWidgets is false, widgets are hidden instead (because setParent(NULL) is extremely slow)
 */
void ClearLayout(QLayout* layout, bool deleteWidgets)
{
	if(layout == NULL || layout->count() == 0) return;

	QLayoutItem *child;

	while (layout->count() > 0) {
		child = layout->takeAt(0);
		if(deleteWidgets && child->widget())
			child->widget()->deleteLater();
		else if(child->widget())
			child->widget()->setVisible(false);

		delete child;
	}

	layout->invalidate();
}


void RemoveLayout(QWidget* widget)
{
	QLayout* layout = widget->layout();
	
	ClearLayout(layout, true);

	delete layout;
}


const std::string htmlEscape(const std::string& s)
{
	return QtUtils::toIndString(QtUtils::toQString(s).toHtmlEscaped());
}


const std::string urlEscape(const std::string& s)
{
	return QtUtils::toIndString(QUrl::toPercentEncoding(QtUtils::toQString(s)));
}


}

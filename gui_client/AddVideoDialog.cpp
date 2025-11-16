/*=====================================================================
AddVideoDialog.cpp
------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "AddVideoDialog.h"


#include "IncludeOpenGL.h"
#include "SubstrataVideoSurface.h"
#include "../qt/QtUtils.h"
#include "../utils/PlatformUtils.h"
#include "../utils/ConPrint.h"
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtCore/QSettings>
#if defined(_WIN32)
#include "../video/WMFVideoReader.h"
#endif


AddVideoDialog::AddVideoDialog(QSettings* settings_, Reference<ResourceManager> resource_manager_, IMFDXGIDeviceManager* dev_manager_)
:	settings(settings_),
	resource_manager(resource_manager_),
	dev_manager(dev_manager_),
	video_width(1920),
	video_height(1080)
{
	setupUi(this);

	// Load main window geometry and state
	this->restoreGeometry(settings->value("AddVideoDialog/geometry").toByteArray());

	this->tabWidget->setCurrentIndex(settings->value("AddVideoDialog/tabIndex", /*default val=*/0).toInt());

	this->fileSelectWidget->setSettingsKey("AddVideoDialog/lastDir");
	this->fileSelectWidget->setFilter("Mp4 files (*.mp4)");

	connect(this->fileSelectWidget, SIGNAL(filenameChanged(QString&)), this, SLOT(filenameChanged(QString&)));
	connect(this->buttonBox, SIGNAL(accepted()), this, SLOT(accepted()));

	connect(this, SIGNAL(finished(int)), this, SLOT(dialogFinished()));
}


AddVideoDialog::~AddVideoDialog()
{
	settings->setValue("AddVideoDialog/geometry", saveGeometry());

	settings->setValue("AddVideoDialog/tabIndex", this->tabWidget->currentIndex());
}


bool AddVideoDialog::wasResultLocalPath()
{
	return tabWidget->currentIndex() == 0;
}


std::string AddVideoDialog::getVideoLocalPath()
{
	return QtUtils::toStdString(fileSelectWidget->filename());
}


std::string AddVideoDialog::getVideoURL()
{
	return QtUtils::toStdString(urlLineEdit->text());
}


// Called when user presses ESC key, or clicks OK or cancel button.
void AddVideoDialog::dialogFinished()
{
}


void AddVideoDialog::accepted()
{
	this->settings->setValue("AddObjectDialogPath", this->fileSelectWidget->filename());
}


void AddVideoDialog::filenameChanged(QString& filename)
{
	const std::string path = QtUtils::toIndString(filename);

	conPrint("AddObjectDialog::filenameChanged: filename = " + path);

	if(filename.isEmpty())
		return;

	getDimensionsForLocalVideoPath(path);
}


void AddVideoDialog::getDimensionsForLocalVideoPath(const std::string& local_path)
{
	this->videoInfoLabel->setText("");

	// Try and load model
	try
	{
		if(hasExtension(local_path, "mp4"))
		{
#if defined(_WIN32)
			Reference<WMFVideoReader> reader = new WMFVideoReader(false, /*just_read_audio=*/false, local_path, dev_manager, /*decode_to_d3d_tex=*/false);

			// Load first frame
			const SampleInfoRef frameinfo = reader->getAndLockNextSample(/*just_get_vid_sample=*/true);

			if(frameinfo.isNull())
				throw glare::Exception("frame was null. (EOS?)");

			this->video_width = (int)frameinfo->width;
			this->video_height = (int)frameinfo->height;

			this->videoInfoLabel->setText(QtUtils::toQString("Video width: " + toString(this->video_width) + " px, height: " + toString(this->video_height) + " px"));

#else
			SubstrataVideoSurface* video_surface = new SubstrataVideoSurface(NULL);
			video_surface->load_into_opengl_tex = false; // Just copy into mem buffer.  OpenGL texture stuff gets tricky with multiple GL contexts.

			QMediaPlayer* media_player = new QMediaPlayer(NULL, QMediaPlayer::VideoSurface);
			media_player->setVideoOutput(video_surface);
			media_player->setMedia(QUrl(QtUtils::toQString(local_path)));
			media_player->play();

			// Wait until we have a valid video frame.
			Timer timer;
			while(timer.elapsed() < 10.0)
			{
				if(media_player->error() != QMediaPlayer::NoError)
					break;

				if(!video_surface->frame_copy.empty()) // We have a frame:
					break;

				QCoreApplication::processEvents();
				PlatformUtils::Sleep(1);
			}

			media_player->stop();

			if(!video_surface->frame_copy.empty())
			{
				this->video_width = video_surface->current_format.frameWidth();
				this->video_height = video_surface->current_format.frameHeight();

				this->videoInfoLabel->setText(QtUtils::toQString("Video width: " + toString(this->video_width) + " px, height: " + toString(this->video_height) + " px"));
			}

			delete media_player;
			delete video_surface;
#endif
		}
		else
			throw glare::Exception("file did not have a supported video extension: '" + getExtension(local_path) + "'");
	}
	catch(glare::Exception& e)
	{
		QtUtils::showErrorMessageDialog(QtUtils::toQString(e.what()), this);
	}
}


// Will be called when the user clicks the 'X' button.
void AddVideoDialog::closeEvent(QCloseEvent* event)
{
}

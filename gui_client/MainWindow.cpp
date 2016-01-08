
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif


#ifdef _MSC_VER // Qt headers suppress some warnings on Windows, make sure the warning suppression doesn't propagate to our code. See https://bugreports.qt.io/browse/QTBUG-26877
#pragma warning(push, 0) // Disable warnings
#endif
#include "MainWindow.h"
//#include "IndigoApplication.h"
#include <QtCore/QTimer>
#include <QtCore/QProcess>
#include <QtCore/QMimeData>
#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGraphicsTextItem>
#include <QtWidgets/QMessageBox>
#include <QtGui/QImageWriter>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QSplashScreen>
#include <QtWidgets/QShortcut>
#ifdef _MSC_VER
#pragma warning(pop) // Re-enable warnings
#endif
#include "GuiClientApplication.h"
#include "../utils/Clock.h"
#include "../utils/PlatformUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Exception.h"
#include "../qt/QtUtils.h"
#include "../graphics/formatdecoderobj.h"
#include "../dll/include/IndigoMesh.h"
#include <clocale>




MainWindow::MainWindow(const std::string& base_dir_path_, const std::string& appdata_path_, const ArgumentParser& args, QWidget *parent)
:	base_dir_path(base_dir_path_),
	appdata_path(appdata_path_),
	parsed_args(args), 
	QMainWindow(parent)
{
	setupUi(this);
}


MainWindow::~MainWindow()
{
}


#if defined(_WIN32) || defined(_WIN64)
#else
#include <signal.h>
#endif


int main(int argc, char *argv[])
{
	GuiClientApplication app(argc, argv);

	// Set the C standard lib locale back to c, so e.g. printf works as normal, and uses '.' as the decimal separator.
	std::setlocale(LC_ALL, "C");

	Clock::init();

	PlatformUtils::ignoreUnixSignals();

	std::string indigo_base_dir_path;
	try
	{
		indigo_base_dir_path = PlatformUtils::getResourceDirectoryPath();
	}
	catch(PlatformUtils::PlatformUtilsExcep& e)
	{
		conPrint(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
		return 1;
	}

	// Get the 'appdata_path', which will be indigo_base_dir_path on Linux/OS-X, but something like
	// 'C:\Users\Nicolas Chapman\AppData\Roaming\Indigo Renderer' on Windows.
	const std::string appdata_path = PlatformUtils::getOrCreateAppDataDirectoryWithDummyFallback();

	QDir::setCurrent(QtUtils::toQString(indigo_base_dir_path));


	// Get a vector of the args.  Note that we will use app.arguments(), because it's the only way to get the args in Unicode in Qt.
	const QStringList arg_list = app.arguments();
	std::vector<std::string> args;
	for(int i = 0; i < arg_list.size(); ++i)
		args.push_back(QtUtils::toIndString(arg_list.at((int)i)));

	try
	{
		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;

		ArgumentParser parsed_args(args, syntax);

		MainWindow mw(indigo_base_dir_path, appdata_path, parsed_args);

		mw.show();

		mw.raise();



		//TEMP:
		// Load a teapot
		try
		{
			Indigo::MeshRef mesh = new Indigo::Mesh();
			FormatDecoderObj::streamModel("N:\\indigo\\trunk\\testfiles\\teapot.obj", *mesh, 1.f);

			mw.glWidget->buildMesh(*mesh);

			GLObjectRef ob = new GLObject();
			ob->materials.resize(1);
			ob->materials[0].albedo_rgb = Colour3f(0.6f, 0.2f, 0.2f);

			ob->pos = Vec3f(0,1,0);
			ob->rotation_axis = Vec3f(0,0,1);
			ob->rotation_angle = 0;
			ob->mesh = mesh;

			mw.glWidget->objects.push_back(ob);
		}
		catch(Indigo::Exception& e)
		{
			conPrint(e.what());
			return 1;
		}




		// Connect IndigoApplication openFileOSX signal to MainWindow openFileOSX slot.
		// It needs to be connected right after handling the possibly early QFileOpenEvent because otherwise it might tigger the slot twice
		// if the event comes in late, between connecting and handling the early signal.
		QObject::connect(&app, SIGNAL(openFileOSX(const QString)), &mw, SLOT(openFileOSX(const QString)));

		return app.exec();
	}
	catch(ArgumentParserExcep& e)
	{
		// Show error
		conPrint(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
		return 1;
	}
}

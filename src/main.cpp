/*
 * r8 One: the QDOS calculator on Sailfish OS.
 *
 * Sets up the three stores, as the Android activity does before SDL starts
 * the shell, then puts the machine on screen and on the cover.
 */

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>
#include <QStandardPaths>
#include <QtQml>

#include <sailfishapp.h>

#include "machine.h"
#include "qdosview.h"

/** A folder and what is in it, as the store holds it */
static void copyTree(const QString& from, const QString& to) {
	QDir().mkpath(to);
	const QDir dir(from);
	for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
		const QString target = to + QLatin1Char('/') + entry.fileName();
		if (entry.isDir()) {
			copyTree(entry.filePath(), target);
		} else {
			QFile::copy(entry.filePath(), target);
		}
	}
}

int main(int argc, char* argv[]) {
	QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
	// Sailjail grants the data directory under these names; see the .desktop file
	app->setOrganizationName(QStringLiteral("rs.r8"));
	app->setApplicationName(QStringLiteral("harbour-r8-one"));

	qmlRegisterType<QdosView>("rs.r8.one", 1, 0, "QdosView");
	qmlRegisterUncreatableType<Machine>("rs.r8.one", 1, 0, "Machine", QStringLiteral("There is the one"));

	/*
	 * The system programs are read where the package installed them, so an
	 * update brings its own. The user store is seeded once, as the firmware
	 * seeds its writable partition. The inbox is in Documents, where a PC can
	 * reach it over USB.
	 */
	const QString programs = SailfishApp::pathTo(QStringLiteral("programs")).toLocalFile();
	const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

	Machine::Stores stores;
	stores.system = programs + QStringLiteral("/system");
	stores.user = data + QStringLiteral("/store");
	stores.inbox = documents + QStringLiteral("/r8 One");
	stores.nativeCache = cache + QStringLiteral("/natives");

	if (!QDir(stores.user).exists()) {
		copyTree(programs + QStringLiteral("/user"), stores.user);
	}

	Machine machine(stores);

	// Sailfish may close an app in the background to free memory, and the
	// shell keeps its session only when it is told to save it
	QObject::connect(app.data(), &QGuiApplication::applicationStateChanged, &machine,
			[&machine](Qt::ApplicationState state) {
				if (state != Qt::ApplicationActive) {
					machine.save();
				}
			});

	// The power key, or auto-off: the calculator is off, so the app goes
	QObject::connect(&machine, &Machine::poweredOff, app.data(), &QGuiApplication::quit);

	QScopedPointer<QQuickView> view(SailfishApp::createView());
	view->rootContext()->setContextProperty(QStringLiteral("calculator"), &machine);
	view->setSource(SailfishApp::pathToMainQml());
	view->show();

	machine.start();
	const int status = app->exec();
	machine.stop();
	return status;
}

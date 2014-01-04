#include "sailfish_inputprocess.h"
#include "sailfishapp/sailfishapp.h"
#include <QtCore/QDebug>
#include <QtGui/QGuiApplication>
#include <QtQuick/QQuickView>
#include <QtCore/QFile>
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>
#include <QtQml/qqml.h>

QGuiApplication *app = NULL;
QQmlContext *context = NULL;
Thing *thing = NULL;
static QObject* thing_provider(QQmlEngine *engine, QJSEngine *scriptEngine)
{
	if(!thing)
		thing = new Thing(app, context);
	return thing;
}

int main(int argc, char *argv[])
{
	app = SailfishApp::application(argc, argv);
	qDebug() << argv[0] << " started";

	fprintf(stderr, "Reading line from standard input...\n");
	QFile in;
	in.open(stdin, QIODevice::ReadOnly);
	QString orig = in.readLine().trimmed();
	fprintf(stderr, "Initial value: \"%s\"\n", orig.toUtf8().data());

	QQuickView *view = SailfishApp::createView();

	//qDebug() << "View base URL:" << view->rootContext()->baseUrl();
	//qDebug() << "Path to foo:" << SailfishApp::pathTo("foo");
	// ^ QUrl( "file:///home/nemo/mt/share/sailfish_inputprocess/foo" )

	context = view->rootContext();
	context->setContextProperty("userInput", orig);

	qmlRegisterSingletonType<Thing>("Thing", 1, 0, "Thing", thing_provider);

    view->setSource(QUrl::fromLocalFile("src/sailfish/inputprocess/main.qml"));
	view->show();
	return app->exec();
}


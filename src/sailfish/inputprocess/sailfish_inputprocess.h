#pragma once
#include <QtCore/QObject>
#include <QtQml/QQmlContext>
#include <QtGui/QGuiApplication>
#include <cstdio>

class Thing: public QObject
{
	Q_OBJECT
public:
	QGuiApplication *m_app;
	QQmlContext *m_context;

	Thing(QGuiApplication *app, QQmlContext *context):
		m_app(app), m_context(context)
	{}

	Q_INVOKABLE void acceptInput(QString result)
	{
		fprintf(stderr, "Outputting input...\n");
		fprintf(stdout, "%s", result.toUtf8().data());
		fflush(stdout);
		fprintf(stderr, "\nDone.\n");
		m_app->exit(0);
	}
};


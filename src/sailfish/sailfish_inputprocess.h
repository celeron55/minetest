#pragma once

#include <QtWidgets/QApplication>
extern QApplication *qt_app;
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLineEdit>
//#include <QtWidgets/QPlainTextEdit>
#include <QtGui/QPalette>
#include <QtWidgets/QSpacerItem>
#include <QtCore/QTimer>
#include <QtCore/QProcess>
#include <QtCore/QFile>
#include <cstdio>

class InputWindow: public QMainWindow
{
	Q_OBJECT
public:
};

class InputApplication: public QApplication
{
	Q_OBJECT

	QLineEdit *m_edit;
public:
	bool cancelled;

	InputApplication(int argc, char *argv[]):
		QApplication(argc, argv),
		cancelled(false)
	{
		fprintf(stderr, "Reading line from standard input...\n");
		QFile in;
		in.open(stdin, QIODevice::ReadOnly);
		QString orig = in.readLine().trimmed();
		fprintf(stderr, "Initial value: \"%s\"\n", orig.toUtf8().data());

		InputWindow *window = new InputWindow();
		QBoxLayout *bl = new QBoxLayout(QBoxLayout::TopToBottom, window);
		QWidget *central = new QWidget();
		central->setLayout(bl);
		window->setCentralWidget(central);
		window->setStyleSheet("background-color: #333333; color: #ffffff");
		window->show();
		QPushButton *button = new QPushButton("Done");
		bl->addWidget(button);
		QLineEdit *edit = new QLineEdit(orig);
		bl->addWidget(edit);
		m_edit = edit;
		bl->addSpacerItem(new QSpacerItem(500,500));
		QTimer::singleShot(0, edit, SLOT(setFocus()));
		QObject::connect(edit, SIGNAL(returnPressed()),
				this, SLOT(gotInput()));
		QObject::connect(button, SIGNAL(clicked()),
				this, SLOT(gotInput()));
		//bl->addWidget(new QPlainTextEdit);
	}

private slots:
	void gotInput()
	{
		fprintf(stderr, "Outputting input...\n");
		fprintf(stdout, "%s", m_edit->text().toUtf8().data());
		fprintf(stderr, "Done.\n");
		quit();
	}
};


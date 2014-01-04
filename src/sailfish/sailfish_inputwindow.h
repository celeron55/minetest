/*
Minetest
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


#ifndef SAILFISH_INPUTWINDOW_HEADER
#define SAILFISH_INPUTWINDOW_HEADER

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

class InputWindow: public QMainWindow
{
		Q_OBJECT
public:
};

inline std::string sailfish_inputwindow_show(const std::string &orig)
{
	QProcess p;
	p.start("jolla-fileman", QStringList());
	if(!p.waitForStarted())
		return orig;
	p.write(orig.c_str());
	p.closeWriteChannel();
	if(!p.waitForFinished())
		return orig;
	QByteArray result = p.readAll();
	return result.data();
#if 0
	InputWindow *window = new InputWindow();
	QBoxLayout *bl = new QBoxLayout(QBoxLayout::TopToBottom, window);
	QWidget *central = new QWidget();
	central->setLayout(bl);
	window->setCentralWidget(central);
	window->setStyleSheet("background-color: #333333; color: #ffffff");
	window->show();
	bl->addWidget(new QPushButton("Done"));
	QLineEdit *input = new QLineEdit();
	bl->addWidget(input);
	bl->addSpacerItem(new QSpacerItem(500,500));
	QTimer::singleShot(0, input, SLOT(setFocus()));
	/*QObject::connect(input, SIGNAL(returnPressed()),
			qt_app, SLOT(quit()));*/
	//bl->addWidget(new QPlainTextEdit);
	for(;;){
		qt_app->processEvents();
		usleep(1000000/60);
	}
	//qt_app->exec();
	return "foo";
#endif
}

#endif

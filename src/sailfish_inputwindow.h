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
#include <QtCore/QProcess>

inline std::string sailfish_inputwindow_show(const std::string &orig)
{
	QProcess p;
	QString path = qt_app->applicationDirPath();
	p.start(path+"/sailfish_inputprocess", QStringList());
	if(!p.waitForStarted())
		return orig;
	p.write(orig.c_str());
	p.closeWriteChannel();
	if(!p.waitForFinished())
		return orig;
	QByteArray result = p.readAll();
	return result.data();
}

#endif

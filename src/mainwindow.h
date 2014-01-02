#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <QtCore>
#include <QApplicationStateChangeEvent>
#include <QWindow>

namespace irr {
    class IrrlichtDevice;
}

class MainApplication: public QApplication
{
    Q_OBJECT
public:
    irr::IrrlichtDevice *device;
    unsigned int bcolor;
    bool quit_requested;

    MainApplication(int argc, char *argv[]):
        QApplication(argc, argv),
        device(NULL),
        bcolor(0),
        quit_requested(false)
    {
        QTimer *timer = new QTimer(this);
        connect(timer, SIGNAL(timeout()), this, SLOT(update()));
		// 10/s is enough to make it look responsive to sailfish
        timer->start(1000.0/10);
        connect(this, SIGNAL(lastWindowClosed()), this, SLOT(requestQuit()));
        connect(this, SIGNAL(aboutToQuit()), this, SLOT(requestQuit()));
    }

    bool event(QEvent *e){
        //qDebug() << "qapplication event:" << e;
        if(e->type() == QEvent::ApplicationStateChange){
            QApplicationStateChangeEvent *asce =
                    static_cast<QApplicationStateChangeEvent*>(e);
            Qt::ApplicationState state = asce->applicationState();
            qDebug() << "event:" << e << "state:" << state;
            // This is hacky but whatever; this way we can close before
            // rendering hangs
            /*if(state == Qt::ApplicationInactive){
                requestQuit();
                return QApplication::event(e);
            }*/
        }
        return QApplication::event(e);
    }

public slots:
    void update();
    void requestQuit();
};

class MainWindow: public QWindow
{
    Q_OBJECT
public:
    bool is_closed;

    MainWindow(QScreen *screen):
        QWindow(screen),
        is_closed(false)
    {}
    bool event(QEvent *e){
        //qDebug() << "qwindow event:" << e;
        if(e->type() == QEvent::Close){
            is_closed = true;
            lolClose();
            return QWindow::event(e);
        }
        return QWindow::event(e);
    }
signals:
    void lolClose();
};

extern MainApplication *g_main_application;

#endif // MAIN_WINDOW_H

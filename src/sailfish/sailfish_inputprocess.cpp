#include "sailfish_inputprocess.h"
#include <cstdio>

int main(int argc, char *argv[])
{
	QApplication *qt_app = new QApplication(argc, argv);
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
	qt_app->exec();
	printf("inputprocess output");
	return 0;
}


#include "sailfish_inputprocess.h"

int main(int argc, char *argv[])
{
	InputApplication *app = new InputApplication(argc, argv);
	app->exec();
	return (app->cancelled ? 1 : 0);
}


/*
 * for QT4
 * 1) qmake qt4.pro
 * 2) make
 */

#include <QApplication>

#include "termwidget.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    TermWidget tw;

    tw.show();

    return app.exec();
}

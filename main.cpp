/*
 * for QT4
 * 1) qmake qt4.pro
 * 2) make
 *
 * for zaurus
 * 1) tmake qtopia.pro > Makefile
 * 2) make
 */

#if defined(USE_QT4)
#include <QApplication>
#elif defined(USE_QTOPIA)
#include <qpe/qpeapplication.h>
#endif

#include "termwidget.h"

int main(int argc, char **argv)
{
#if defined(USE_QT4)
    QApplication app(argc, argv);
#elif defined(USE_QTOPIA)
    QPEApplication app(argc, argv);
#endif
    TermWidget tw;

#if defined(USE_QTOPIA)
    app.setMainWidget(&tw);
#endif

    tw.show();

    return app.exec();
}

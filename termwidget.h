#ifndef _TERMWIDGET_H_
#define _TERMWIDGET_H_

#if defined(USE_QT4)
#include <QWidget>
#elif defined(USE_QTOPIA)
#include <qwidget.h>
#endif

class TermWidget: public QWidget
{
public:
    TermWidget();
};

#endif /* _TERMWIDGET_H_ */

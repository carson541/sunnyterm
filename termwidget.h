#ifndef _TERMWIDGET_H_
#define _TERMWIDGET_H_

#if defined(USE_QT4)
#include <QWidget>
#include <QKeyEvent>
#include <QTimer>
#elif defined(USE_QTOPIA)
#include <qwidget.h>
#endif

class TermWidget: public QWidget
{
    Q_OBJECT

public:
    TermWidget();

private slots:
    void  doRead();

protected:
    void paintEvent(QPaintEvent *);
    void keyPressEvent(QKeyEvent *);

private:
    QTimer *readTimer;
};

#endif /* _TERMWIDGET_H_ */

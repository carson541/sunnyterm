#ifndef _TERMWIDGET_H_
#define _TERMWIDGET_H_

#if defined(USE_QT4)
#include <QWidget>
#include <QFont>
#include <QKeyEvent>
#include <QTimer>
#include <QInputMethodEvent>
#elif defined(USE_QTOPIA)
#include <qwidget.h>
#include <qtimer.h>
#endif

class TermWidget: public QWidget
{
    Q_OBJECT

public:
    TermWidget();
	bool setCellFont(QFont &font);

private slots:
    void  doRead();

protected:
    void paintEvent(QPaintEvent *);
    void keyPressEvent(QKeyEvent *);
#if defined(USE_QT4)
    void inputMethodEvent(QInputMethodEvent *);
//    QVariant inputMethodQuery(Qt::InputMethodQuery property) const;
#endif

private:
    QTimer *readTimer;
	QFont cell_font;
};

#endif /* _TERMWIDGET_H_ */

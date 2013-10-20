#if defined(USE_QT4)
#include <QWidget>
#include <QFont>
#include <QKeyEvent>
#include <QTimer>
#include <QColor>
#include <QPainter>
#elif defined(USE_QTOPIA)
#include <qwidget.h>
#include <qtimer.h>
#include <qpainter.h>
#endif

#include "termwidget.h"

#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* term */
#define COLS 80
#define ROWS 24

struct t_cell {
    char c;
};

struct t_cell cell[ROWS][COLS];
int dirty[ROWS];
int cursor_x, cursor_y;
int cursor_state;
int esc;

void treset(void);
void tputc(char c);

/* screen */
void draw(QWidget *w);
void xdraws(QPainter &painter, QString str, int x, int y, int len);

/* widget */
pid_t g_pid;
int g_fd;

int	cell_width = 16;
int	cell_height = 16;

void sigchld(int)
{
	int stat = 0;
	if(waitpid(g_pid, &stat, 0) < 0) {
        printf("Waiting for pid filed.\n");
    }
	if(WIFEXITED(stat))
		exit(WEXITSTATUS(stat));
	else
		exit(EXIT_FAILURE);
}

void ttyread(void)
{
	char buf[256];
	int count;
    char c;
    int i;

    count = read(g_fd, buf, sizeof(buf));

    if(count > 0) {
        qDebug("read %d", count);
    }

#if 1
    // dump
    for(i = 0; i < count; i++) {
        if(i % 16 == 0 && i != 0) printf("\n");
        printf("%.2x ", buf[i]);
    }
    printf("\n");
#endif

    for(i = 0; i < count; i++) {
        c = buf[i];
        tputc(c);
    }
}

TermWidget::TermWidget()
{
	pid_t pid;
	int fd;

//	QFont font("Terminus");
//	QFont font("Ubuntu Mono");
//    cell_font = font;
//	font.setPixelSize(12);
	QFont font("Bitstream Vera Sans Mono", 15);
//	font.setPixelSize(20);
//    font.setFixedPitch(true);
//    font.setKerning(false);
	setCellFont(font);

    QFontInfo info(font);
    QString family = info.family();
    qDebug("family = %s", family.toStdString().c_str());

    setGeometry(600, 200, cell_width * COLS + 2, cell_height * ROWS);

    if((pid = forkpty(&fd, NULL, NULL, NULL)) < 0) {
		perror("forkpty");
        return;
	}

	if (pid == 0) {
        static const char *argv[] = {
            "bash", 0
        };
		execve("/bin/bash", (char * const *)argv, NULL);
    }

//    qDebug("father");

    g_pid = pid;
    g_fd = fd;

    signal(SIGCHLD, sigchld);

    readTimer = new QTimer(this);
    connect(readTimer, SIGNAL(timeout()),
	     this, SLOT(doRead()));
    readTimer->start(1000);

    treset();
}

bool TermWidget::setCellFont(QFont &font)
{
	cell_font = font;
	cell_font.setFixedPitch(true);
	cell_font.setKerning(false);
	cell_font.setStyleHint(QFont::TypeWriter);

	QFontMetrics fm(font);
	cell_width  = fm.width('X');
	cell_height = fm.height();
    qDebug("cell_width = %d", cell_width);
    qDebug("cell_height = %d", cell_height);

	QFontMetrics qm(font);
//    qDebug("%d", qm.width("Hello"));
//    qDebug("%d", qm.width("I"));

    return true;
}

void TermWidget::doRead()
{
//    qDebug("doRead");
	fd_set rfd;
    FD_ZERO(&rfd);
    FD_SET(g_fd, &rfd);
	struct timeval timeout = {0,0};
    timeout.tv_sec  = 0;
    timeout.tv_usec = 20 * 1000; // 20 ms
//	bool stuff_to_print = 0;

    if(select(g_fd + 1, &rfd, NULL, NULL, &timeout) < 0) {
        return;
    }

    if(FD_ISSET(g_fd, &rfd)) {
        ttyread();
//        stuff_to_print = 1;
    }

    draw(this);
}

void TermWidget::paintEvent(QPaintEvent *)
{
//    qDebug("paintEvent");

#if defined(USE_QT4)
	QPainter painter(this);
//	QString str =  "01234567890123456789";
//	QString str2 = "testtesttesttesttesb";

    painter.setFont(cell_font);

//	QRect rect(0, 0, 16 * 40, 16);
//	painter.drawText(rect, Qt::TextSingleLine, str);

//	QRect rect2(0, 16, 16 * 40, 16 * 2);
//	painter.drawText(rect2, Qt::TextSingleLine, str2);

    int x, y;
    for(y = 0; y < ROWS; y++) {
        QString str;
        for(x = 0; x < COLS; x++) {
            str += QChar(cell[y][x].c);
        }
        // if(y == 0) {
        //     qDebug("str = %s", str.toStdString().c_str());
        // }
        xdraws(painter, str, 0, y, COLS);
    }

#elif defined(USE_QTOPIA)
	QPainter painter(this);
	QString str = "test";

	QRect rect(0, 0, 16 * 4, 16);
	painter.drawText(rect, Qt::SingleLine, str);

	QRect rect2(0, 16, 16 * 4, 16);
	painter.drawText(rect2, Qt::SingleLine, str);
#endif
}

void TermWidget::keyPressEvent(QKeyEvent *k)
{
#if defined(USE_QT4)
    qDebug("<keyPressEvent> - %d, %s",
           k->key(),
           k->text().toStdString().c_str());
#elif defined(USE_QTOPIA)
    qDebug("<keyPressEvent> - %d", k->key());
#endif
	if(k->text().isEmpty()) {
        qDebug("text empty");
    } else {
        qDebug("text not empty");
    }

    switch(k->key()) {
    case Qt::Key_Return:
        qDebug("<keyPressEvent>: Key_Return");
        break;
    default:
        break;
    }

	if(k->text().isEmpty()) {
        return;
    }

#if defined(USE_QT4)
    QByteArray data = k->text().toLocal8Bit();
	int n = write(g_fd, data.constData(), data.count());
#elif defined(USE_QTOPIA)
    QByteArray data = k->text().local8Bit();
	int n = write(g_fd, data.data(), data.count());
#endif
    qDebug("write: %d", n);
}

/* term */
#define CURSOR_DEFAULT        0
#define CURSOR_WRAPNEXT       1

void tsetchar(char c);
void tmoveto(int x, int y);
void tnewline(int first_col);

void treset(void)
{
    int i, j;

    for(i = 0; i < ROWS; i++) {
        for(j = 0; j < COLS; j++) {
            // if(j == 0) {
            //     cell[i][j].c = 'a';
            // } else if (j == 2) {
            //     cell[i][j].c = (i + 1) / 10 + '0';
            // } else if (j == 3) {
            //     cell[i][j].c = (i + 1) % 10 + '0';
            // } else if (j == COLS-1) {
            //     cell[i][j].c = 'b';
            // } else {
                cell[i][j].c = ' ';
            // }
        }
        dirty[i] = 1;
    }

    cursor_x = 0, cursor_y = 0;
    cursor_state = CURSOR_DEFAULT;

    esc = 0;
}

void tputc(char c)
{
    switch(c) {
    default:
        if(cursor_state & CURSOR_WRAPNEXT) {
            tnewline(1);
        }
        tsetchar(c);
        if(cursor_x + 1 < COLS) {
            tmoveto(cursor_x + 1, cursor_y);
        } else {
            cursor_state |= CURSOR_WRAPNEXT;
        }
        break;
    }
}

void tsetchar(char c)
{
    cell[cursor_y][cursor_x].c = c;
    dirty[cursor_y] = 1;
}

void tmoveto(int x, int y)
{
    if(x < 0) x = 0;
    if(x > COLS - 1) x = COLS - 1;
    if(y < 0) y = 0;
    if(y > ROWS - 1) y = ROWS - 1;

    cursor_x = x;
    cursor_y = y;
    cursor_state &= ~CURSOR_WRAPNEXT;
}

void tnewline(int first_col)
{
    int y = cursor_y;
    y++;

    tmoveto(first_col ? 0 : cursor_x, y);
}

/* screen */
void draw(QWidget *w)
{
    w->update();
}

void xdraws(QPainter &painter, QString str, int x, int y, int len)
{
//	QString str;
//    str += "aaa ";
//    str += QChar((y + 1) / 10 + '0');
//    str += QChar((y + 1) % 10 + '0');
	QRect rect(x * cell_width, y * cell_height,
		cell_width * len, cell_height);
	painter.drawText(rect, Qt::TextSingleLine, str);
}

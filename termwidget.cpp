#if defined(USE_QT4)
#include <QWidget>
#include <QKeyEvent>
#include <QTimer>
#include <QColor>
#include <QPainter>
#elif defined(USE_QTOPIA)
#include <qwidget.h>
#include <qtimer.h>
#endif

#include "termwidget.h"

#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

void treset(void);
void tputc(char c);

void draw(QWidget *w);

/* widget */
pid_t g_pid;
int g_fd;

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

#if 1                                           \
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

    qDebug("father");

    g_pid = pid;
    g_fd = fd;

    signal(SIGCHLD, sigchld);

    readTimer = new QTimer(this);
    connect(readTimer, SIGNAL(timeout()),
	     this, SLOT(doRead()));
    readTimer->start(1000);

    treset();
}

void TermWidget::doRead()
{
    qDebug("doRead");
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
    qDebug("paintEvent");

#if defined(USE_QT4)
	QPainter painter(this);
	QString str = "test";

	QRect rect(0, 0, 16 * 4, 16);
	painter.drawText(rect, Qt::TextSingleLine, str);

	QRect rect2(0, 16, 16 * 4, 16);
	painter.drawText(rect2, Qt::TextSingleLine, str);
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
#define COLS 80
#define ROWS 24

struct t_cell {
    char c;
};

struct t_cell cell[ROWS][COLS];
int dirty[ROWS];
int cursor_x, cursor_y;
int esc;

void tsetchar(char c);

void treset(void)
{
    int i, j;

    for(i = 0; i < ROWS; i++) {
        for(j = 0; j < COLS; j++) {
            cell[i][j].c = 'a';
        }
        dirty[i] = 1;
    }

    cursor_x = 0, cursor_y = 0;

    esc = 0;
}

void tputc(char c)
{
    switch(c) {
    default:
        tsetchar(c);
        break;
    }
}

void tsetchar(char c)
{
//    cell[cursor_y][cursor_x].c = c;
//    dirty[cursor_y] = 1;
}

/* screen */
void draw(QWidget *w)
{
    w->update();
}

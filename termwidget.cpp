#if defined(USE_QT4)
#include <QWidget>
#include <QFont>
#include <QKeyEvent>
#include <QTimer>
#include <QColor>
#include <QPainter>
#include <QTextCodec>
#elif defined(USE_QTOPIA)
#include <qwidget.h>
#include <qfont.h>
#include <qtimer.h>
#include <qcolor.h>
#include <qpainter.h>
#endif

#include "termwidget.h"

#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* term */
#define COLS 80
#define ROWS 24

#define GLYPH_SET             1

#define ATTR_NULL             0
#define ATTR_REVERSE          1
#define ATTR_UNDERLINE        2
#define ATTR_BOLD             4
#define ATTR_BLINK            8

#define UTF_SIZ               4

#define LEN(a)     (sizeof(a) / sizeof(a[0]))

struct t_cell {
    char c[UTF_SIZ];
    int mode;
    int fg;
    int bg;
    int state;
};

struct t_cell cell[ROWS][COLS];
int dirty[ROWS];
int cursor_mode;
int cursor_fg, cursor_bg;
int cursor_x, cursor_y;
int cursor_state;

void treset(void);
void tputc(char *c);

/* screen */
#define DefaultFG             7
#define DefaultBG             0

static const QColor palette_xterm[] = { /* AARRGGBB */
    0xff000000, /* black */
    0xffcd0000, /* red3 */
    0xff00cd00, /* green3 */
    0xffcdcd00, /* yellow3 */
    0xff0000ee, /* blue2 */
    0xffcd00cd, /* magenta3 */
    0xff00cdcd, /* cyan3 */
    0xffe5e5e5, /* gray90 */
    0xff7f7f7f, /* gray50 */
    0xffff0000, /* red */
    0xff00ff00, /* green */
    0xffffff00, /* yellow */
    0xff5c5cff,
    0xffff00ff, /* magenta */
    0xff00ffff, /* cyan */
    0xffffffff  /* white */
};

void draw(QWidget *w);
void xdraws(QPainter &painter,
            int mode, int fg, int bg,
            QString str, int x, int y, int charlen, int bytelen);
void xdrawcursor(QPainter &painter);
void xclear(QPainter &painter, int x1, int y1, int x2, int y2);

/* widget */
pid_t g_pid;
int g_fd;

int cell_width = 16;
int cell_height = 16;

void sigchld(int)
{
    int stat = 0;
    if(waitpid(g_pid, &stat, 0) < 0) {
        printf("Waiting for pid failed.\n");
    }
    if(WIFEXITED(stat))
        exit(WEXITSTATUS(stat));
    else
        exit(EXIT_FAILURE);
}

/* bit macro */
#undef B0
enum { B0=1, B1=2, B2=4, B3=8, B4=16, B5=32, B6=64, B7=128 };

int utf8decode(char *s, long *u) {
    uchar c;
    int i, n, rtn;

    rtn = 1;
    c = *s;
    if(~c & B7) { /* 0xxxxxxx */
        *u = c;
        return rtn;
    } else if((c & (B7|B6|B5)) == (B7|B6)) { /* 110xxxxx */
        *u = c&(B4|B3|B2|B1|B0);
        n = 1;
    } else if((c & (B7|B6|B5|B4)) == (B7|B6|B5)) { /* 1110xxxx */
        *u = c&(B3|B2|B1|B0);
        n = 2;
    } else if((c & (B7|B6|B5|B4|B3)) == (B7|B6|B5|B4)) { /* 11110xxx */
        *u = c & (B2|B1|B0);
        n = 3;
    } else
        goto invalid;
    for(i = n, ++s; i > 0; --i, ++rtn, ++s) {
        c = *s;
        if((c & (B7|B6)) != B7) /* 10xxxxxx */
            goto invalid;
        *u <<= 6;
        *u |= c & (B5|B4|B3|B2|B1|B0);
    }
    if((n == 1 && *u < 0x80) ||
       (n == 2 && *u < 0x800) ||
       (n == 3 && *u < 0x10000) ||
       (*u >= 0xD800 && *u <= 0xDFFF))
        goto invalid;
    return rtn;
invalid:
    *u = 0xFFFD;
    return rtn;
}

int utf8encode(long *u, char *s) {
    uchar *sp;
    ulong uc;
    int i, n;

    sp = (uchar*) s;
    uc = *u;
    if(uc < 0x80) {
        *sp = uc; /* 0xxxxxxx */
        return 1;
    } else if(*u < 0x800) {
        *sp = (uc >> 6) | (B7|B6); /* 110xxxxx */
        n = 1;
    } else if(uc < 0x10000) {
        *sp = (uc >> 12) | (B7|B6|B5); /* 1110xxxx */
        n = 2;
    } else if(uc <= 0x10FFFF) {
        *sp = (uc >> 18) | (B7|B6|B5|B4); /* 11110xxx */
        n = 3;
    } else {
        goto invalid;
    }
    for(i=n,++sp; i>0; --i,++sp)
        *sp = ((uc >> 6*(i-1)) & (B5|B4|B3|B2|B1|B0)) | B7; /* 10xxxxxx */
    return n+1;
invalid:
    /* U+FFFD */
    *s++ = '\xEF';
    *s++ = '\xBF';
    *s = '\xBD';
    return 3;
}

/* use this if your buffer is less than UTF_SIZ, it returns 1 if you can decode
   UTF-8 otherwise return 0 */
int isfullutf8(char *s, int b) {
    uchar *c1, *c2, *c3;

    c1 = (uchar *) s;
    c2 = (uchar *) ++s;
    c3 = (uchar *) ++s;
    if(b < 1)
        return 0;
    else if((*c1&(B7|B6|B5)) == (B7|B6) && b == 1)
        return 0;
    else if((*c1&(B7|B6|B5|B4)) == (B7|B6|B5) &&
        ((b == 1) ||
        ((b == 2) && (*c2&(B7|B6)) == B7)))
        return 0;
    else if((*c1&(B7|B6|B5|B4|B3)) == (B7|B6|B5|B4) &&
        ((b == 1) ||
        ((b == 2) && (*c2&(B7|B6)) == B7) ||
        ((b == 3) && (*c2&(B7|B6)) == B7 && (*c3&(B7|B6)) == B7)))
        return 0;
    else
        return 1;
}

int utf8size(char *s) {
    uchar c = *s;

    if(~c&B7)
        return 1;
    else if((c&(B7|B6|B5)) == (B7|B6))
        return 2;
    else if((c&(B7|B6|B5|B4)) == (B7|B6|B5))
        return 3;
    else
        return 4;
}

void ttyread(void)
{
    static char buf[256];
    static int buflen = 0;
    char *ptr;
    char s[UTF_SIZ];
    int charsize; /* size of utf8 char in bytes */
    long utf8c;
    int ret;

    /* append read bytes to unprocessed bytes */
    ret = read(g_fd, buf+buflen, LEN(buf) - buflen);
    if(ret > 0) {
//        qDebug("read %d", ret);
    }

#if 0
    // dump
    for(int i = 0; i < ret; i++) {
        if(i % 16 == 0 && i != 0) printf("\n");
        printf("%.2x ", (unsigned char)buf[i+buflen]);
    }
    printf("\n");
#endif

    /* process every complete utf8 char */
    buflen += ret;
    ptr = buf;
    while(buflen >= UTF_SIZ || isfullutf8(ptr,buflen)) {
        charsize = utf8decode(ptr, &utf8c);
        memset(s, 0, UTF_SIZ);
        utf8encode(&utf8c, s);
//        printf("%s",s);
        tputc(s);
        ptr    += charsize;
        buflen -= charsize;
    }

    /* keep any uncomplete utf8 char for the next call */
    memmove(buf, ptr, buflen);
}

TermWidget::TermWidget()
{
    pid_t pid;
    int fd;

#if defined(USE_QT4)
//    QFont font("Bitstream Vera Sans Mono", 15);
    QFont font("Ubuntu Mono", 15);
#elif defined(USE_QTOPIA)
    QFont font("Terminus", 16);
#endif
    setCellFont(font);

    QFontInfo info(font);
    QString family = info.family();
#if defined(USE_QT4)
    // qDebug("family = %s", family.toStdString().c_str());
#elif defined(USE_QTOPIA)
    // qDebug("family = %s", family.latin1());
#endif

#if defined(USE_QT4)
    setGeometry(600, 200, cell_width * COLS + 2, cell_height * ROWS);
#elif defined(USE_QTOPIA)
    setGeometry(10, 30, cell_width * COLS + 2, cell_height * ROWS);
#endif

    if((pid = forkpty(&fd, NULL, NULL, NULL)) < 0) {
        perror("forkpty");
        return;
    }

    if (pid == 0) {
        static const char *argv[] = {
            "bash", 0
        };
        execve("/bin/bash", (char * const *)argv, NULL);
//        execve("test/test", (char * const *)argv, NULL);
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
#if defined(USE_QT4)
    cell_font.setKerning(false);
#endif
    cell_font.setStyleHint(QFont::TypeWriter);

    QFontMetrics fm(font);
    cell_width  = fm.width('X');
    cell_height = fm.height();
    // qDebug("cell_width = %d", cell_width);
    // qDebug("cell_height = %d", cell_height);
    // QString s1 = QString::fromUtf8("test");
    // QString s2 = QString::fromUtf8("中文");
    // qDebug("cell_width_en = %d", fm.width(s1));
    // qDebug("cell_width_zh = %d", fm.width(s2));

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
//  bool stuff_to_print = 0;

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

    QPainter painter(this);

#if 0 // GBK
    QTextCodec *codec = QTextCodec::codecForName("GBK");
#endif

#if 0 // debug, test
    painter.setFont(cell_font);
    const char *s = "test\xd6\xd0\xce\xc4test";
    QString s2 = QString::fromUtf8("中文test中文");

    QByteArray line = QByteArray(s);
    QString str = codec->toUnicode(line);
    painter.drawText(10, 40, str);
    painter.drawText(10, 40+24, s2);
    return;
#endif

    int x, y;
    int ic, ib, ox, sl;
    int base_mode, new_mode;
    int base_fg, new_fg;
    int base_bg, new_bg;
    int new_state;
    char new_c[UTF_SIZ];

    for(y = 0; y < ROWS; y++) {
        xclear(painter, 0, y, COLS, y);

        QString str;
#if 0 // GBK
        QByteArray btr;
#endif
        ic = ib = ox = 0;

        for(x = 0; x < COLS; x++) {
            new_state = cell[y][x].state;
            new_mode = cell[y][x].mode;
            new_fg = cell[y][x].fg;
            new_bg = cell[y][x].bg;
            memcpy(new_c, cell[y][x].c, UTF_SIZ);

            if(ib > 0 && (!(new_state & GLYPH_SET) ||
                          (base_mode != new_mode) ||
                          (base_fg != new_fg) ||
                          (base_bg != new_bg))) {
#if defined(USE_QT4)
                if(base_mode & ATTR_BOLD) {
                    cell_font.setBold(true);
                } else {
                    cell_font.setBold(false);
                }
#endif
                painter.setFont(cell_font);
#if 0 // GBK
                str = codec->toUnicode(btr);
#endif
                xdraws(painter, base_mode, base_fg, base_bg, str, ox, y, ic, ib);
                ic = ib = 0;
            }

            if(new_state & GLYPH_SET) {
                if(ib == 0) {
#if defined(USE_QT4)
                    str.clear();
#if 0 // GBK
                    btr.clear();
#endif
#elif defined(USE_QTOPIA)
                    str.truncate(0);
#endif
                    ox = x;
                    base_mode = new_mode;
                    base_fg = new_fg;
                    base_bg = new_bg;
                }
                sl = utf8size(new_c);
#if 0 // GBK
                btr.append(cell[y][x].c);
#else
                QString s = QString::fromLocal8Bit(cell[y][x].c);
                str += s;
#endif
                ib += sl;
                ic ++;
            }
        }
        // if(y == 0) {
        //     qDebug("str = %s", str.toStdString().c_str());
        // }
        if(ib > 0) {
#if defined(USE_QT4)
            if(base_mode & ATTR_BOLD) {
                cell_font.setBold(true);
            } else {
                cell_font.setBold(false);
            }
#endif
            painter.setFont(cell_font);
#if 0 // GBK
            str = codec->toUnicode(btr);
#endif
            xdraws(painter, base_mode, base_fg, base_bg, str, ox, y, ic, ib);
        }
    }

    xdrawcursor(painter);
}

void TermWidget::keyPressEvent(QKeyEvent *k)
{
#if defined(USE_QT4)
    // qDebug("<keyPressEvent> - %d, %s",
    //        k->key(),
    //        k->text().toStdString().c_str());
#elif defined(USE_QTOPIA)
    // qDebug("<keyPressEvent> - %d", k->key());
#endif
    if(k->text().isEmpty()) {
//        qDebug("text empty");
    } else {
//        qDebug("text not empty");
    }

    switch(k->key()) {
    case Qt::Key_Return:
        // qDebug("<keyPressEvent>: Key_Return");
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
    // qDebug("write: %d", n);
    n = n; // dummy
}

/* term */
#define ESC_BUF_SIZ   256
#define ESC_ARG_SIZ   16

#define CURSOR_DEFAULT        0
#define CURSOR_WRAPNEXT       1

#define ESC_START             1
#define ESC_CSI               2

/* CSI Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode>] */
typedef struct {
    char buf[ESC_BUF_SIZ]; /* raw string */
    int len;               /* raw string length */
    char priv;
    int arg[ESC_ARG_SIZ];
    int narg;              /* nb of args */
    char mode;
} CSIEscape;

static CSIEscape escseq;

int term_top, term_bottom;
int esc;

void tsetchar(char *c);
void tmoveto(int x, int y);
void tnewline(int first_col);
void tscrollup(int orig, int n);
void tclearregion(int x1, int y1, int x2, int y2);

void csireset(void);
void csiparse(void);
void csihandle(void);
void csidump(void);

void tsetattr(int *attr, int l);

void treset(void)
{
    tclearregion(0, 0, COLS - 1, ROWS - 1);

    cursor_mode = ATTR_NULL;
    cursor_fg = DefaultFG, cursor_bg = DefaultBG;
    cursor_x = 0, cursor_y = 0;
    cursor_state = CURSOR_DEFAULT;

    term_top = 0, term_bottom = ROWS - 1;

    esc = 0;
}

void tputc(char *c)
{
    char ascii = *c;
    if(esc & ESC_START) {
        if(esc & ESC_CSI) {
            escseq.buf[escseq.len++] = ascii;
            if((ascii >= 0x40 && ascii <= 0x7E) || escseq.len >= ESC_BUF_SIZ) {
                esc = 0;
                csiparse();
                csihandle();
            }
        } else {
            switch(ascii) {
            case '[':
                esc |= ESC_CSI;
                break;
            default:
                printf("unknown sequence ESC 0x%02X '%c'\n",
                       ascii, isprint(ascii)? ascii : '.');
                esc = 0;
                break;
            }
        }
    } else {
        switch(ascii) {
        case '\b': // BS
            tmoveto(cursor_x - 1, cursor_y);
            break;
        case '\r': // CR
            tmoveto(0, cursor_y);
            break;
        case '\n': // LF
            tnewline(0);
            break;
        case '\a': // BEL
            break;
        case '\033': // ESC
            csireset();
            esc = ESC_START;
            break;
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
}

void tsetchar(char *c)
{
    cell[cursor_y][cursor_x].mode = cursor_mode;
    cell[cursor_y][cursor_x].fg = cursor_fg;
    cell[cursor_y][cursor_x].bg = cursor_bg;

    memcpy(cell[cursor_y][cursor_x].c, c, UTF_SIZ);

    cell[cursor_y][cursor_x].state |= GLYPH_SET;

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
    if(y == term_bottom) {
        tscrollup(term_top, 1);
    } else {
        y++;
    }

    tmoveto(first_col ? 0 : cursor_x, y);
}

void tscrollup(int orig, int n)
{
    int i, j;
    t_cell temp;

    if(n < 0) n = 0;
    if(n > term_bottom - orig + 1) n = term_bottom - orig + 1;

    tclearregion(0, orig, COLS - 1, orig + n - 1);

    for(i = orig; i <= term_bottom - n; i++) {
        for(j = 0; j < COLS; j++) {
            temp = cell[i][j];
            cell[i][j] = cell[i+n][j];
            cell[i+n][j] = temp;
        }

        dirty[i] = 1;
        dirty[i+n] = 1;
    }
}

void tclearregion(int x1, int y1, int x2, int y2)
{
    int x, y, temp;

    if(x1 > x2)
        temp = x1, x1 = x2, x2 = temp;
    if(y1 > y2)
        temp = y1, y1 = y2, y2 = temp;

    if(x1 < 0) x1 = 0;
    if(x1 > COLS - 1) x1 = COLS - 1;
    if(y1 < 0) y1 = 0;
    if(y1 > ROWS - 1) y1 = ROWS - 1;

    if(x2 < 0) x2 = 0;
    if(x2 > COLS - 1) x2 = COLS - 1;
    if(y2 < 0) y2 = 0;
    if(y2 > ROWS - 1) y2 = ROWS - 1;

    for(y = y1; y <= y2; y++) {
        for(x = x1; x <= x2; x++) {
            cell[y][x].state = 0;
        }
        dirty[y] = 1;
    }
}

void csireset(void)
{
    memset(&escseq, 0, sizeof(escseq));
}

void csiparse(void)
{
    /* int noarg = 1; */
    char *p = escseq.buf;

    escseq.narg = 0;
    if(*p == '?')
        escseq.priv = 1, p++;

    while(p < escseq.buf + escseq.len) {
        while(isdigit(*p)) {
            escseq.arg[escseq.narg] *= 10;
            escseq.arg[escseq.narg] += *p++ - '0'/*, noarg = 0 */;
        }
        if(*p == ';' && escseq.narg+1 < ESC_ARG_SIZ)
            escseq.narg++, p++;
        else {
            escseq.mode = *p;
            escseq.narg++;
            return;
        }
    }
}

void csihandle(void)
{
    switch(escseq.mode) {
    case 'H': /* CUP -- Move to <row> <col> */
        if(!escseq.arg[0]) escseq.arg[0] = 1;
        if(!escseq.arg[1]) escseq.arg[1] = 1;
        tmoveto(escseq.arg[1] - 1, escseq.arg[0] - 1);
        break;
    case 'I': /* font */
        switch(escseq.arg[0]) {
        case 0: // back to normal
            break;
        case 1: // SongTi
            break;
        case 2: // HeiTi
            break;
        case 3: // KaiTi
            break;
        default:
            break;
        }
        break;
    case 'J': /* ED -- Clear screen */
        switch(escseq.arg[0]) {
        case 0: /* below */
            tclearregion(cursor_x, cursor_y, COLS - 1, cursor_y);
            if(cursor_y < ROWS - 1)
                tclearregion(0, cursor_y + 1, COLS - 1, ROWS - 1);
            break;
        case 1: /* above */
            if(cursor_y > 1)
                tclearregion(0, 0, COLS - 1, cursor_y - 1);
            tclearregion(0, cursor_y, cursor_x, cursor_y);
            break;
        case 2: /* all */
            tclearregion(0, 0, COLS - 1, ROWS - 1);
            break;
        default:
            printf("unknown csi ");
            csidump();
            break;
        }
        break;
    case 'K': /* EL -- Clear line */
        switch(escseq.arg[0]) {
        case 0: /* right */
            tclearregion(cursor_x, cursor_y, COLS - 1, cursor_y);
            break;
        case 1: /* left */
            tclearregion(0, cursor_y, cursor_x, cursor_y);
            break;
        case 2: /* all */
            tclearregion(0, cursor_y, COLS - 1, cursor_y);
            break;
        }
        break;
    case 'm': /* SGR -- Terminal attribute (color) */
        tsetattr(escseq.arg, escseq.narg);
        break;
    default:
        printf("unknown csi ");
        csidump();
        break;
    }
}

void csidump(void)
{
    int i;
    printf("ESC[");
    for(i = 0; i < escseq.len; i++) {
        uint c = escseq.buf[i] & 0xff;
        if(isprint(c)) putchar(c);
        else if(c == '\n') printf("(\\n)");
        else if(c == '\r') printf("(\\r)");
        else if(c == 0x1b) printf("(\\e)");
        else printf("(%02x)", c);
    }
    putchar('\n');
}

void tsetattr(int *attr, int l)
{
    int i;

    for(i = 0; i < l; i++) {
        switch(attr[i]) {
        case 0:
            cursor_mode &= ~(ATTR_REVERSE | ATTR_UNDERLINE | ATTR_BOLD);
            cursor_fg = DefaultFG;
            cursor_bg = DefaultBG;
            break;
        case 1:
            cursor_mode |= ATTR_BOLD;
            break;
        case 5:
            cursor_mode |= ATTR_BLINK;
            break;
        case 7:
            cursor_mode |= ATTR_REVERSE;
            break;
        default:
            if(attr[i] >= 30 && attr[i] <= 37) {
                cursor_fg = attr[i] - 30;
            } else if(attr[i] >= 40 && attr[i] <= 47) {
                cursor_bg = attr[i] - 40;
            } else {
                printf("erresc: gfx attr %d unknown\n", attr[i]), csidump();
            }
            break;
        }
    }
}

/* screen */
void draw(QWidget *w)
{
    w->update();
}

void xdraws(QPainter &painter,
            int mode, int fg, int bg,
            QString str, int x, int y, int charlen, int bytelen)
{
    int temp;

    if(mode & ATTR_REVERSE) {
        temp = fg, fg = bg, bg = temp;
    }

    if(mode & ATTR_BOLD) {
        if(fg < 8) {
            fg += 8;
        }
    }

    QColor color;
    color = palette_xterm[fg];

    QRect rect(x * cell_width, y * cell_height,
        cell_width * bytelen, cell_height);

    painter.fillRect(rect, palette_xterm[bg]);

    painter.setPen(color);

#if defined(USE_QT4)
    painter.drawText(rect, Qt::TextSingleLine, str);
#elif defined(USE_QTOPIA)
    painter.drawText(rect, Qt::SingleLine, str);
#endif

    charlen = charlen;
}

void xdrawcursor(QPainter &painter)
{
    QRect rect(cursor_x * cell_width, cursor_y * cell_height,
        cell_width, cell_height);

    QRect r = rect;

#if defined(USE_QT4)
    r.adjust(1, 1, -1, -1);
#endif

    painter.drawRect(r);
}

void xclear(QPainter &painter, int x1, int y1, int x2, int y2)
{
    QRect rect(x1 * cell_width, y1 * cell_height,
               cell_width * (x2 - x1 + 1), cell_height * (y2 - y1 + 1));

    painter.fillRect(rect, palette_xterm[cursor_bg]);
}

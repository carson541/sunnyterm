TEMPLATE = app

TARGET = sunnyterm

DEFINES += USE_QT4

DEPENDPATH += .
INCLUDEPATH += .

HEADERS += termwidget.h
SOURCES += main.cpp termwidget.cpp

LIBS = -lutil

QMAKE_CLEAN += sunnyterm

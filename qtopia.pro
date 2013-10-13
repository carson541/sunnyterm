TEMPLATE        = app
CONFIG          = qt warn_on debug

DEFINES         += USE_QTOPIA

HEADERS         = termwidget.h
SOURCES         = main.cpp termwidget.cpp

INCLUDEPATH	+= $(QPEDIR)/include
DEPENDPATH	+= $(QPEDIR)/include
LIBS            += -lqpe -lutil
INTERFACES	=
TARGET		= sunnyterm

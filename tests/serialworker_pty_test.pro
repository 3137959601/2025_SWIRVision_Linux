QT += core serialport
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = serialworker_pty_test

INCLUDEPATH += ..
SOURCES += \
    serialworker_pty_test.cpp \
    ../serialworker.cpp \
    ../uartprotocol.cpp

HEADERS += \
    ../serialworker.h \
    ../uartprotocol.h

unix: LIBS += -lutil

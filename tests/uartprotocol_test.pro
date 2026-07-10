QT += core
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = uartprotocol_test

SOURCES += \
    uartprotocol_test.cpp \
    ../uartprotocol.cpp

HEADERS += ../uartprotocol.h

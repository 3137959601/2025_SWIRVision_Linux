QT       += core gui openglwidgets serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

#QMAKE_CXXFLAGS_DEBUG = -Zi -MDd
QMAKE_LFLAGS += "/STACK:655360000,40960000" #设置栈保留大小6553600k，提交大小409600k
CONFIG += resources_big
SOURCES += \
    common/device.cpp \
    common/myCombox/comboxItem.cpp \
    common/myCombox/myCombox.cpp \
    common/tih_usb_device.cpp \
    gl_image_widget.cpp \
    image_processor.cpp \
    main.cpp \
    mainwindow.cpp \
    serialworker.cpp \
    telemetrydebugdialog.cpp \
    transfer_thread.cpp \
    uartprotocol.cpp \
    widget_image.cpp

HEADERS += \
    common/device.h \
    common/myCombox/comboxItem.h \
    common/myCombox/myCombox.h \
    common/tih_usb_device.h \
    common/winusb_supp.h \
    gl_image_widget.h \
    image_processor.h \
    mainwindow.h \
    serialworker.h \
    telemetrydebugdialog.h \
    transfer_thread.h \
    uartprotocol.h \
    widget_image.h

FORMS += \
    mainwindow.ui

LIBS += -lSetupAPI
LIBS += -luser32
LIBS += -lcfgmgr32
LIBS += -lwinusb

win32:CONFIG(release, debug|release): LIBS += -LD:/software/opencv/opencv/build/x64/vc16/lib/ -lopencv_world4100
else:win32:CONFIG(debug, debug|release): LIBS += -LD:/software/opencv/opencv/build/x64/vc16/lib/ -lopencv_world4100d
else:unix: LIBS += -LD:/software/opencv/opencv/build/x64/vc16/lib/ -lopencv_world4100

INCLUDEPATH += D:/software/opencv/opencv/build/include
DEPENDPATH += D:/software/opencv/opencv/build/include

RC_ICONS = icons/Infrared_image_acquisition_system.ico

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    icons.qrc

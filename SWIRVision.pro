QT += core gui widgets serialport
greaterThan(QT_MAJOR_VERSION, 5): QT += openglwidgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

#QMAKE_CXXFLAGS_DEBUG = -Zi -MDd
win32: QMAKE_LFLAGS += "/STACK:655360000,40960000" # 仅MSVC链接器支持
CONFIG += resources_big
SOURCES += \
    common/myCombox/comboxItem.cpp \
    common/myCombox/myCombox.cpp \
    gl_image_widget.cpp \
    image_processor.cpp \
    linearstretchcalibrationdialog.cpp \
    main.cpp \
    mainwindow.cpp \
    serialworker.cpp \
    telemetrydebugdialog.cpp \
    uartprotocol.cpp \
    widget_image.cpp

win32: SOURCES += \
    common/device.cpp \
    common/tih_usb_device.cpp \
    transfer_thread.cpp

unix: SOURCES += \
    common/device_linux.cpp \
    common/tih_usb_device_linux.cpp \
    transfer_thread_linux.cpp

HEADERS += \
    common/device.h \
    common/myCombox/comboxItem.h \
    common/myCombox/myCombox.h \
    common/tih_usb_device.h \
    common/usb_types.h \
    common/winusb_supp.h \
    gl_image_widget.h \
    image_processor.h \
    linearstretchcalibrationdialog.h \
    linearstretchmath.h \
    mainwindow.h \
    serialworker.h \
    telemetrydebugdialog.h \
    transfer_thread.h \
    uartprotocol.h \
    widget_image.h

FORMS += \
    mainwindow.ui

win32: LIBS += -lSetupAPI -luser32 -lcfgmgr32 -lwinusb

win32:CONFIG(release, debug|release): LIBS += -LD:/software/opencv/opencv/build/x64/vc16/lib/ -lopencv_world4100
else:win32:CONFIG(debug, debug|release): LIBS += -LD:/software/opencv/opencv/build/x64/vc16/lib/ -lopencv_world4100d
else:unix {
    CONFIG += link_pkgconfig
    PKGCONFIG += opencv4 gl
    QMAKE_CXXFLAGS += -fopenmp
    QMAKE_LFLAGS += -fopenmp
}

win32 {
    INCLUDEPATH += D:/software/opencv/opencv/build/include
    DEPENDPATH += D:/software/opencv/opencv/build/include
}

win32: RC_ICONS = icons/Infrared_image_acquisition_system.ico

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    icons.qrc

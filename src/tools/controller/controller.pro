TEMPLATE = app
TARGET   = appman-controller

load(am-config)

QT = core network dbus
QT *= appman_common-private

CONFIG *= console

SOURCES +=  \
    controller.cpp \

appmanif.files =  ../../dbus-lib/io.qt.applicationmanager.xml
appmanif.header_flags = -i dbus-utilities.h

DBUS_INTERFACES += \
    ../../dbus-lib/io.qt.applicationinstaller.xml \
    appmanif

load(qt_tool)

load(install-prefix)

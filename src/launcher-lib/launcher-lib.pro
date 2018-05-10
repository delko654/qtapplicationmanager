TEMPLATE = lib
TARGET = QtAppManLauncher
MODULE = appman_launcher

load(am-config)

QT = qml dbus core-private
!headless:QT += quick gui gui-private quick-private
QT_FOR_PRIVATE *= \
    appman_common-private \
    appman_shared_main-private \
    appman_application-private \
    appman_notification-private \

CONFIG *= static internal_module

SOURCES += \
    qmlapplicationinterface.cpp \
    ipcwrapperobject.cpp \
    qmlapplicationinterfaceextension.cpp \
    qmlnotification.cpp \
    launchermain.cpp

!headless:SOURCES += \
    applicationmanagerwindow.cpp \

HEADERS += \
    qmlapplicationinterface.h \
    ipcwrapperobject.h \
    ipcwrapperobject_p.h \
    qmlapplicationinterfaceextension.h \
    qmlnotification.h \
    launchermain.h

!headless:HEADERS += \
    applicationmanagerwindow_p.h

load(qt_module)

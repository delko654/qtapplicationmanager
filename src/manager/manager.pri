load(am-config)

QT = core network qml core-private
!headless:QT *= gui quick
qtHaveModule(dbus):QT *= dbus
qtHaveModule(pssdp):QT *= pssdp
qtHaveModule(pshellserver):QT *= pshellserver
QT *= \
    appman_common-private \
    appman_application-private \
    appman_manager-private \
    appman_installer-private \
    appman_notification-private \

CONFIG *= console

win32:LIBS += -luser32

multi-process:!headless {
    qtHaveModule(waylandcompositor) {
        QT *= waylandcompositor waylandcompositor-private
        HEADERS += $$PWD/waylandcompositor.h
        SOURCES += $$PWD/waylandcompositor.cpp
        PKGCONFIG += wayland-server
    } else:qtHaveModule(compositor) {
        QT *= compositor
        HEADERS += $$PWD/waylandcompositor-old.h
        SOURCES += $$PWD/waylandcompositor-old.cpp
    }
}

HEADERS += \
    $$PWD/qmllogger.h \
    $$PWD/configuration.h \

!headless:HEADERS += \
    $$PWD/inprocesswindow.h \
    $$PWD/waylandwindow.h \
    $$PWD/windowmanager.h \
    $$PWD/windowmanager_p.h \

SOURCES += \
    $$PWD/main.cpp \
    $$PWD/qmllogger.cpp \
    $$PWD/configuration.cpp \

!headless:SOURCES += \
    $$PWD/inprocesswindow.cpp \
    $$PWD/waylandwindow.cpp \
    $$PWD/windowmanager.cpp \

DBUS_ADAPTORS += \
    $$PWD/../dbus/io.qt.applicationinstaller.xml \

!headless:DBUS_ADAPTORS += \
    $$PWD/../dbus/io.qt.windowmanager.xml \

# this is a bit more complicated than it should be, but qdbusxml2cpp cannot
# cope with more than 1 out value out of the box
# http://lists.qt-project.org/pipermail/interest/2013-July/008011.html
dbus-notifications.files = $$PWD/../dbus/org.freedesktop.notifications.xml
dbus-notifications.source_flags = -l QtAM::NotificationManager
dbus-notifications.header_flags = -l QtAM::NotificationManager -i notificationmanager.h

dbus-appman.files = $$PWD/../dbus/io.qt.applicationmanager.xml
dbus-appman.source_flags = -l QtAM::ApplicationManager
dbus-appman.header_flags = -l QtAM::ApplicationManager -i applicationmanager.h

DBUS_ADAPTORS += dbus-notifications dbus-appman

load(qt_tool)

load(install-prefix)

OTHER_FILES = \
    syms.txt \

load(build-config)

unix:exists($$SOURCE_DIR/.git):GIT_VERSION=$$system(cd "$$SOURCE_DIR" && git describe --tags --always --dirty 2>/dev/null)
isEmpty(GIT_VERSION):GIT_VERSION="unknown"

createBuildConfig(_DATE_, VERSION, GIT_VERSION, SOURCE_DIR, BUILD_DIR, INSTALL_PREFIX, \
                  QT_ARCH, QT_VERSION, QT, CONFIG, DEFINES, INCLUDEPATH, LIBS)

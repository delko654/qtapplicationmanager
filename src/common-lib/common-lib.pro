TEMPLATE = lib
TARGET = QtAppManCommon
MODULE = appman_common

load(am-config)

QT = core network concurrent
qtHaveModule(geniviextras):QT *= geniviextras
android:QT *= androidextras
qtHaveModule(dbus):QT *= dbus
qtHaveModule(qml):QT *= core-private qml qml-private

versionAtLeast(QT.geniviextras.VERSION, 1.1.0) {
    DEFINES += AM_GENIVIEXTRAS_LAZY_INIT
}

CONFIG *= static internal_module

include($$SOURCE_DIR/3rdparty/libyaml.pri)
contains(DEFINES, "AM_USE_LIBBACKTRACE"):include($$SOURCE_DIR/3rdparty/libbacktrace.pri)

SOURCES += \
    exception.cpp \
    utilities.cpp \
    qtyaml.cpp \
    startuptimer.cpp \
    unixsignalhandler.cpp \
    processtitle.cpp \
    crashhandler.cpp \
    logging.cpp \
    dbus-utilities.cpp \

qtHaveModule(qml):SOURCES += \
    qml-utilities.cpp \

HEADERS += \
    global.h \
    error.h \
    exception.h \
    utilities.h \
    qtyaml.h \
    startuptimer.h \
    unixsignalhandler.h \
    processtitle.h \
    crashhandler.h \
    logging.h

qtHaveModule(qml):HEADERS += \
    qml-utilities.h \

qtHaveModule(dbus):HEADERS += \
    dbus-utilities.h \

load(qt_module)

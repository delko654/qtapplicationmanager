TEMPLATE = app
TARGET   = appman-launcher-qml

load(am-config)

QT = qml dbus core-private
!headless:QT += quick gui gui-private quick-private
QT *= \
    appman_common-private \
    appman_notification-private \
    appman_application-private \
    appman_plugininterfaces-private \
    appman_launcher-private \

SOURCES += \
    main.cpp \

load(qt_tool)

load(install-prefix)

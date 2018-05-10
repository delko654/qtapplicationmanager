/****************************************************************************
**
** Copyright (C) 2018 Pelagicore AG
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Pelagicore Application Manager.
**
** $QT_BEGIN_LICENSE:LGPL-QTAS$
** Commercial License Usage
** Licensees holding valid commercial Qt Automotive Suite licenses may use
** this file in accordance with the commercial license agreement provided
** with the Software or, alternatively, in accordance with the terms
** contained in a written agreement between you and The Qt Company.  For
** licensing terms and conditions see https://www.qt.io/terms-conditions.
** For further information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
** SPDX-License-Identifier: LGPL-3.0
**
****************************************************************************/

#include <QFileInfo>
#include <QCoreApplication>
#include <QDebug>

#include <QtAppManCommon/logging.h>

#include "defaultconfiguration.h"

#if !defined(AM_CONFIG_FILE)
#  define AM_CONFIG_FILE "/opt/am/config.yaml"
#endif

QT_BEGIN_NAMESPACE_AM


DefaultConfiguration::DefaultConfiguration(const char *additionalDescription,
                                           bool onlyOnePositionalArgument)
    : DefaultConfiguration(QStringList { qSL(AM_CONFIG_FILE) }, qSL(":/build-config.yaml"),
                           additionalDescription, onlyOnePositionalArgument)
{ }

DefaultConfiguration::DefaultConfiguration(const QStringList &defaultConfigFilePaths,
                                           const QString &buildConfigFilePath,
                                           const char *additionalDescription,
                                           bool onlyOnePositionalArgument)
    : Configuration(defaultConfigFilePaths, buildConfigFilePath)
    , m_onlyOnePositionalArgument(onlyOnePositionalArgument)
{
    // using QStringLiteral for all strings here adds a few KB of ro-data, but will also improve
    // startup times slightly: less allocations and copies. MSVC cannot cope with multi-line though

    const char *description =
        "In addition to the commandline options below, the following environment\n"
        "variables can be set:\n\n"
        "  AM_STARTUP_TIMER  if set to 1, a startup performance analysis will be printed\n"
        "                    on the console. Anything other than 1 will be interpreted\n"
        "                    as the name of a file that is used instead of the console.\n"
        "\n"
        "  AM_FORCE_COLOR_OUTPUT  can be set to 'on' to force color output to the console\n"
        "                         and to 'off' to disable it. Any other value will result\n"
        "                         in the default, auto-detection behavior.\n";

    m_clp.setApplicationDescription(QCoreApplication::organizationName() + qL1C(' ')
                                    + QCoreApplication::applicationName() + qSL("\n\n")
                                    + (additionalDescription ? (qL1S(additionalDescription) + qSL("\n\n")) : QString())
                                    + qL1S(description));

    m_clp.addPositionalArgument(qSL("qml-file"),   qSL("the main QML file."));
    m_clp.addOption({ qSL("database"),             qSL("application database."), qSL("file"), qSL("/opt/am/apps.db") });
    m_clp.addOption({ { qSL("r"), qSL("recreate-database") },  qSL("recreate the application database.") });
    m_clp.addOption({ qSL("builtin-apps-manifest-dir"),   qSL("base directory for built-in application manifests."), qSL("dir") });
    m_clp.addOption({ qSL("installed-apps-manifest-dir"), qSL("base directory for installed application manifests."), qSL("dir"), qSL("/opt/am/manifests") });
    m_clp.addOption({ qSL("app-image-mount-dir"),  qSL("base directory where application images are mounted to."), qSL("dir"), qSL("/opt/am/image-mounts") });
#if defined(QT_DBUS_LIB)
#  if defined(Q_OS_LINUX)
    const QString defaultDBus = qSL("session");
#  else
    const QString defaultDBus = qSL("none");
#  endif
    m_clp.addOption({ qSL("dbus"),                 qSL("register on the specified D-Bus."), qSL("<bus>|system|session|none"), defaultDBus });
    m_clp.addOption({ qSL("start-session-dbus"),   qSL("start a private session bus instead of using an existing one.") });
#endif
    m_clp.addOption({ qSL("fullscreen"),           qSL("display in full-screen.") });
    m_clp.addOption({ qSL("no-fullscreen"),        qSL("do not display in full-screen.") });
    m_clp.addOption({ qSL("I"),                    qSL("additional QML import path."), qSL("dir") });
    m_clp.addOption({ qSL("verbose"),              qSL("verbose output.") });
    m_clp.addOption({ qSL("slow-animations"),      qSL("run all animations in slow motion.") });
    m_clp.addOption({ qSL("load-dummydata"),       qSL("loads QML dummy-data.") });
    m_clp.addOption({ qSL("no-security"),          qSL("disables all security related checks (dev only!)") });
    m_clp.addOption({ qSL("no-ui-watchdog"),       qSL("disables detecting hung UI applications (e.g. via Wayland's ping/pong).") });
    m_clp.addOption({ qSL("no-dlt-logging"),       qSL("disables logging using automotive DLT.") });
    m_clp.addOption({ qSL("force-single-process"), qSL("forces single-process mode even on a wayland enabled build.") });
    m_clp.addOption({ qSL("force-multi-process"),  qSL("forces multi-process mode. Will exit immediately if this is not possible.") });
    m_clp.addOption({ qSL("wayland-socket-name"),  qSL("use this file name to create the wayland socket."), qSL("socket") });
    m_clp.addOption({ qSL("single-app"),           qSL("runs a single application only (ignores the database)"), qSL("info.yaml file") });
    m_clp.addOption({ qSL("logging-rule"),         qSL("adds a standard Qt logging rule."), qSL("rule") });
    m_clp.addOption({ qSL("qml-debug"),            qSL("enables QML debugging and profiling.") });
    m_clp.addOption({ qSL("enable-touch-emulation"), qSL("enables the touch emulation, converting mouse to touch events.") });
}

DefaultConfiguration::~DefaultConfiguration()
{
}


void DefaultConfiguration::parse()
{
    Configuration::parse();

    if (m_onlyOnePositionalArgument && (m_clp.positionalArguments().size() > 1)) {
        showParserMessage(qL1S("Only one main qml file can be specified.\n"), ErrorMessage);
        exit(1);
    }
}

QString DefaultConfiguration::mainQmlFile() const
{
    if (!m_clp.positionalArguments().isEmpty() && m_clp.positionalArguments().at(0).endsWith(qL1S(".qml")))
        return m_clp.positionalArguments().at(0);
    else
        return value<QString>(nullptr, { "ui", "mainQml" });
}


QString DefaultConfiguration::database() const
{
    return value<QString>("database", { "applications", "database" });
}

bool DefaultConfiguration::recreateDatabase() const
{
    return value<bool>("recreate-database");
}

QStringList DefaultConfiguration::builtinAppsManifestDirs() const
{
    return value<QStringList>("builtin-apps-manifest-dir", { "applications", "builtinAppsManifestDir" });
}

QString DefaultConfiguration::installedAppsManifestDir() const
{
    return value<QString>("installed-apps-manifest-dir", { "applications", "installedAppsManifestDir" });
}

QString DefaultConfiguration::appImageMountDir() const
{
    return value<QString>("app-image-mount-dir", { "applications", "appImageMountDir" });
}

bool DefaultConfiguration::fullscreen() const
{
    return value<bool>("fullscreen", { "ui", "fullscreen" });
}

bool DefaultConfiguration::noFullscreen() const
{
    return value<bool>("no-fullscreen");
}

QString DefaultConfiguration::windowIcon() const
{
    return value<QString>(nullptr, { "ui", "windowIcon" });
}

QStringList DefaultConfiguration::importPaths() const
{
    QStringList importPaths = value<QStringList>("I", { "ui", "importPaths" });

    for (int i = 0; i < importPaths.size(); ++i)
        importPaths[i] = QFileInfo(importPaths.at(i)).absoluteFilePath();

    return importPaths;
}

bool DefaultConfiguration::verbose() const
{
    return value<bool>("verbose");
}

bool DefaultConfiguration::slowAnimations() const
{
    return value<bool>("slow-animations");
}

bool DefaultConfiguration::loadDummyData() const
{
    return value<bool>("load-dummydata", { "ui", "loadDummyData" });
}

bool DefaultConfiguration::noSecurity() const
{
    return value<bool>("no-security", { "flags", "noSecurity" });
}

bool DefaultConfiguration::noUiWatchdog() const
{
    return value<bool>("no-ui-watchdog", { "flags", "noUiWatchdog" });
}

bool DefaultConfiguration::noDltLogging() const
{
    return value<bool>("no-dlt-logging");
}

bool DefaultConfiguration::forceSingleProcess() const
{
    return value<bool>("force-single-process", { "flags", "forceSingleProcess" });
}

bool DefaultConfiguration::forceMultiProcess() const
{
    return value<bool>("force-multi-process", { "flags", "forceMultiProcess" });
}

bool DefaultConfiguration::qmlDebugging() const
{
    return value<bool>("qml-debug");
}

QString DefaultConfiguration::singleApp() const
{
    return value<QString>("single-app");
}

QStringList DefaultConfiguration::loggingRules() const
{
    return value<QStringList>("logging-rule", { "logging", "rules" });
}

QString DefaultConfiguration::style() const
{
    return value<QString>(nullptr, { "ui", "style" });
}

bool DefaultConfiguration::enableTouchEmulation() const
{
    return value<bool>("enable-touch-emulation", { "ui", "enableTouchEmulation" });
}

QVariantMap DefaultConfiguration::openGLConfiguration() const
{
    return value<QVariant>(nullptr, { "ui", "opengl" }).toMap();
}

QVariantList DefaultConfiguration::installationLocations() const
{
    return value<QVariant>(nullptr, { "installationLocations" }).toList();
}

QList<QPair<QString, QString>> DefaultConfiguration::containerSelectionConfiguration() const
{
    QList<QPair<QString, QString>> config;
    QVariant containerSelection = value<QVariant>(nullptr, { "containers", "selection" });

    // this is easy to get wrong in the config file, so we do not just ignore a map here
    // (this will in turn trigger the warning below)
    if (containerSelection.type() == QVariant::Map)
        containerSelection = QVariantList { containerSelection };

    if (containerSelection.type() == QVariant::String) {
        config.append(qMakePair(qSL("*"), containerSelection.toString()));
    } else if (containerSelection.type() == QVariant::List) {
        QVariantList list = containerSelection.toList();
        for (const QVariant &v : list) {
            if (v.type() == QVariant::Map) {
                QVariantMap map = v.toMap();

                if (map.size() != 1) {
                    qCWarning(LogSystem) << "The container selection configuration needs to be a list of "
                                            "single mappings, in order to preserve the evaluation "
                                            "order: found a mapping with" << map.size() << "entries.";
                }

                for (auto it = map.cbegin(); it != map.cend(); ++it)
                    config.append(qMakePair(it.key(), it.value().toString()));
            }
        }
    }
    return config;
}

QVariantMap DefaultConfiguration::containerConfigurations() const
{
    QVariantMap map = value<QVariant>(nullptr, { "containers" }).toMap();
    map.remove(qSL("selection"));
    return map;
}

QVariantMap DefaultConfiguration::runtimeConfigurations() const
{
    return value<QVariant>(nullptr, { "runtimes" }).toMap();
}

QVariantMap DefaultConfiguration::dbusPolicy(const char *interfaceName) const
{
    return value<QVariant>(nullptr, { "dbus", interfaceName, "policy" }).toMap();
}

QString DefaultConfiguration::dbusRegistration(const char *interfaceName) const
{
    QString dbus = value<QString>("dbus", { "dbus", interfaceName, "register" });
    if (dbus == qL1S("none"))
        dbus.clear();
    return dbus;
}

int DefaultConfiguration::dbusRegistrationDelay() const
{
    return qMax(0, value<QVariant>(nullptr, { "dbus", "registrationDelay" }).toInt());
}

bool DefaultConfiguration::dbusStartSessionBus() const
{
    return value<bool>("start-session-dbus", { "dbus", "startSessionBus" });
}

QVariantMap DefaultConfiguration::rawSystemProperties() const
{
    return value<QVariant>(nullptr, { "systemProperties" }).toMap();
}

bool DefaultConfiguration::applicationUserIdSeparation(uint *minUserId, uint *maxUserId, uint *commonGroupId) const
{
    bool found = false;
    QVariantMap map = value<QVariant>(nullptr, { "installer", "applicationUserIdSeparation" }).toMap();

    if (found) {
        auto idFromMap = [&map](const char *key) -> uint {
            bool ok;
            uint value = map.value(qL1S(key)).toUInt(&ok);
            return ok ? value : uint(-1);
        };

        uint undef = uint(-1);
        uint minUser = idFromMap("minUserId");
        uint maxUser = idFromMap("maxUserId");
        uint commonGroup = idFromMap("commonGroupId");

        if (minUser != undef && maxUser != undef && commonGroup != undef && minUser < maxUser) {
            if (minUserId)
                *minUserId = minUser;
            if (maxUserId)
                *maxUserId = maxUser;
            if (commonGroupId)
                *commonGroupId = commonGroup;
            return true;
        }
    }
    return false;
}

qreal DefaultConfiguration::quickLaunchIdleLoad() const
{
    return value<QVariant>(nullptr, { "quicklaunch", "idleLoad" }).toReal();
}

int DefaultConfiguration::quickLaunchRuntimesPerContainer() const
{
    int rpc = value<QVariant>(nullptr, { "quicklaunch", "runtimesPerContainer" }).toInt();

    // if you need more than 10 quicklaunchers per runtime, you're probably doing something wrong
    // or you have a typo in your YAML, which could potentially freeze your target (container
    // construction can be expensive)
    return qBound(0, rpc, 10);
}

QString DefaultConfiguration::waylandSocketName() const
{
    return value<QString>("wayland-socket-name");
}

QString DefaultConfiguration::telnetAddress() const
{
    QString s = value<QString>(nullptr, { "debug", "telnetAddress" });
    if (s.isEmpty())
        s = qSL("0.0.0.0");
    return s;
}

quint16 DefaultConfiguration::telnetPort() const
{
    return value<QVariant>(nullptr, { "debug", "telnetPort" }).value<quint16>();
}

QVariantMap DefaultConfiguration::managerCrashAction() const
{
    return value<QVariant>(nullptr, { "crashAction"} ).toMap();
}

QStringList DefaultConfiguration::caCertificates() const
{
    return value<QStringList>(nullptr, { "installer", "caCertificates" });
}

QStringList DefaultConfiguration::pluginFilePaths(const char *type) const
{
    return value<QStringList>(nullptr, { "plugins", type });
}

QStringList DefaultConfiguration::testRunnerArguments() const
{
    QStringList targs = m_clp.positionalArguments();
    if (!targs.isEmpty() && targs.constFirst().endsWith(qL1S(".qml")))
        targs.removeFirst();
    targs.prepend(QCoreApplication::arguments().constFirst());
    return targs;
}

QT_END_NAMESPACE_AM

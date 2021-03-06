/****************************************************************************
**
** Copyright (C) 2016 Pelagicore AG
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Pelagicore Application Manager.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT-QTAS$
** Commercial License Usage
** Licensees holding valid commercial Qt Automotive Suite licenses may use
** this file in accordance with the commercial license agreement provided
** with the Software or, alternatively, in accordance with the terms
** contained in a written agreement between you and The Qt Company.  For
** licensing terms and conditions see https://www.qt.io/terms-conditions.
** For further information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QStringList>
#include <QDir>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QDBusConnection>
#include <QDBusError>

#include "global.h"
#include "error.h"
#include "exception.h"
#include "applicationmanager_interface.h"
#include "applicationinstaller_interface.h"
#include "qtyaml.h"
#include "dbus-utilities.h"

QT_USE_NAMESPACE_AM

class DBus : public QObject {
public:
    DBus()
    {
        registerDBusTypes();
    }

    void connectToManager() throw(Exception)
    {
        if (m_manager)
            return;

        auto conn = connectTo(qSL("io.qt.ApplicationManager"));
        m_manager = new IoQtApplicationManagerInterface(qSL("io.qt.ApplicationManager"), qSL("/ApplicationManager"), conn, this);
    }

    void connectToInstaller() throw(Exception)
    {
        if (m_installer)
            return;

        auto conn = connectTo(qSL("io.qt.ApplicationInstaller"));
        m_installer = new IoQtApplicationInstallerInterface(qSL("io.qt.ApplicationManager"), qSL("/ApplicationInstaller"), conn, this);
    }

private:
    QDBusConnection connectTo(const QString &iface) throw(Exception)
    {
        QDBusConnection conn(iface);

        QFile f(QDir::temp().absoluteFilePath(QString(qSL("%1.dbus")).arg(iface)));
        QString dbus;
        if (f.open(QFile::ReadOnly)) {
            dbus = QString::fromUtf8(f.readAll());
            if (dbus == qL1S("system")) {
                conn = QDBusConnection::systemBus();
                dbus = qSL("[system-bus]");
            } else if (dbus.isEmpty()) {
                conn = QDBusConnection::sessionBus();
                dbus = qSL("[session-bus]");
            } else {
                conn = QDBusConnection::connectToBus(dbus, qSL("custom"));
            }
        } else {
            throw Exception(Error::IO, "Could not find the D-Bus interface of a running Application-Manager instance.\n(did you start the appman with '--dbus none'?");
        }

        if (!conn.isConnected()) {
            throw Exception(Error::IO, "Could not connect to the Application-Manager D-Bus interface %1 at %2: %3")
                .arg(iface, dbus, conn.lastError().message());
        }
        return conn;
    }

public:
    IoQtApplicationInstallerInterface *installer() const
    {
        return m_installer;
    }

    IoQtApplicationManagerInterface *manager() const
    {
        return m_manager;
    }

private:
    IoQtApplicationInstallerInterface *m_installer = nullptr;
    IoQtApplicationManagerInterface *m_manager = nullptr;
};

static class DBus dbus;


enum Command {
    NoCommand,
    StartApplication,
    DebugApplication,
    StopApplication,
    ListApplications,
    ShowApplication,
    InstallPackage,
    RemovePackage,
    ListInstallationLocations,
    ShowInstallationLocation
};

static struct {
    Command command;
    const char *name;
    const char *description;
} commandTable[] = {
    { StartApplication, "start-application", "Start an application." },
    { DebugApplication, "debug-application", "Debug an application." },
    { StopApplication,  "stop-application",  "Stop an application." },
    { ListApplications, "list-applications", "List all installed applications." },
    { ShowApplication,  "show-application",  "Show application meta-data." },
    { InstallPackage,   "install-package",   "Install a package." },
    { RemovePackage,    "remove-package",    "Remove a package." },
    { ListInstallationLocations, "list-installation-locations", "List all installaton locations." },
    { ShowInstallationLocation,  "show-installation-location",  "Show details for installation location." }
};

static Command command(QCommandLineParser &clp)
{
    if (!clp.positionalArguments().isEmpty()) {
        QByteArray cmd = clp.positionalArguments().at(0).toLatin1();

        for (uint i = 0; i < sizeof(commandTable) / sizeof(commandTable[0]); ++i) {
            if (cmd == commandTable[i].name) {
                clp.clearPositionalArguments();
                clp.addPositionalArgument(cmd, commandTable[i].description, cmd);
                return commandTable[i].command;
            }
        }
    }
    return NoCommand;
}

static void startApplication(const QString &appId, const QMap<QString, int> &stdRedirections, const QString &documentUrl);
static void debugApplication(const QString &debugWrapper, const QString &appId, const QMap<QString, int> &stdRedirections, const QString &documentUrl);
static void stopApplication(const QString &appId);
static void listApplications();
static void showApplication(const QString &appId);
static void installPackage(const QString &package, const QString &location) throw(Exception);
static void removePackage(const QString &package, bool keepDocuments, bool force) throw(Exception);
static void listInstallationLocations();
static void showInstallationLocation(const QString &location);

class ThrowingApplication : public QCoreApplication
{
public:
    ThrowingApplication(int &argc, char **argv)
        : QCoreApplication(argc, argv)
    { }

    Exception *exception() const
    {
        return m_exception;
    }

protected:
    bool notify(QObject *o, QEvent *e)
    {
        try {
            return QCoreApplication::notify(o, e);
        } catch (const Exception &e) {
            m_exception = new Exception(e);
            exit(3);
            return true;
        }
    }
private:
    Exception *m_exception = nullptr;
};


int main(int argc, char *argv[])
{
    QCoreApplication::setApplicationName(qSL("ApplicationManager Controller"));
    QCoreApplication::setOrganizationName(qSL("Pelagicore AG"));
    QCoreApplication::setOrganizationDomain(qSL("pelagicore.com"));
    QCoreApplication::setApplicationVersion(qSL(AM_VERSION));

    ThrowingApplication a(argc, argv);

    QString desc = qSL("\nPelagicore ApplicationManager controller tool\n\nAvailable commands are:\n");
    uint longestName = 0;
    for (uint i = 0; i < sizeof(commandTable) / sizeof(commandTable[0]); ++i)
        longestName = qMax(longestName, qstrlen(commandTable[i].name));
    for (uint i = 0; i < sizeof(commandTable) / sizeof(commandTable[0]); ++i) {
        desc += qSL("  %1%2  %3\n")
                .arg(qL1S(commandTable[i].name),
                     QString(longestName - qstrlen(commandTable[i].name), qL1C(' ')),
                     qL1S(commandTable[i].description));
    }

    desc += qSL("\nMore information about each command can be obtained by running\n  appman-controller <command> --help");

    QCommandLineParser clp;
    clp.setApplicationDescription(desc);

    clp.addHelpOption();
    clp.addVersionOption();

    clp.addPositionalArgument(qSL("command"), qSL("The command to execute."));

    // ignore unknown options for now -- the sub-commands may need them later
    clp.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);

    if (!clp.parse(QCoreApplication::arguments())) {
        fprintf(stderr, "%s\n", qPrintable(clp.errorText()));
        exit(1);
    }
    clp.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);

    try {
        switch (command(clp)) {
        default:
        case NoCommand:
            if (clp.isSet(qSL("version"))) {
#if QT_VERSION < QT_VERSION_CHECK(5,4,0)
                fprintf(stdout, "%s %s\n", qPrintable(QCoreApplication::applicationName()), qPrintable(QCoreApplication::applicationVersion()));
                exit(0);
#else
                clp.showVersion();
#endif
            }
            if (clp.isSet(qSL("help")))
                clp.showHelp();
            clp.showHelp(1);
            break;

        case StartApplication: {
            clp.addOption({ { qSL("i"), qSL("attach-stdin") }, qSL("Attach the app's stdin to the controller's stdin") });
            clp.addOption({ { qSL("o"), qSL("attach-stdout") }, qSL("Attach the app's stdout to the controller's stdout") });
            clp.addOption({ { qSL("e"), qSL("attach-stderr") }, qSL("Attach the app's stderr to the controller's stderr") });
            clp.addPositionalArgument(qSL("application-id"), qSL("The id of an installed application."));
            clp.addPositionalArgument(qSL("document-url"),   qSL("The optional document-url."), qSL("[document-url]"));
            clp.process(a);

            int args = clp.positionalArguments().size();
            if (args < 2 || args > 3)
                clp.showHelp(1);

            QMap<QString, int> stdRedirections;
            if (clp.isSet(qSL("attach-stdin")))
                stdRedirections[qSL("in")] = 0;
            if (clp.isSet(qSL("attach-stdout")))
                stdRedirections[qSL("out")] = 1;
            if (clp.isSet(qSL("attach-stderr")))
                stdRedirections[qSL("err")] = 2;

            startApplication(clp.positionalArguments().at(1), stdRedirections, args == 3 ? clp.positionalArguments().at(2) : QString());
            break;
        }
        case DebugApplication: {
            clp.addOption({ { qSL("i"), qSL("attach-stdin") }, qSL("Attach the app's stdin to the controller's stdin") });
            clp.addOption({ { qSL("o"), qSL("attach-stdout") }, qSL("Attach the app's stdout to the controller's stdout") });
            clp.addOption({ { qSL("e"), qSL("attach-stderr") }, qSL("Attach the app's stderr to the controller's stderr") });
            clp.addPositionalArgument(qSL("debug-wrapper"),  qSL("The name of a configured debug-wrapper."));
            clp.addPositionalArgument(qSL("application-id"), qSL("The id of an installed application."));
            clp.addPositionalArgument(qSL("document-url"),   qSL("The optional document-url."), qSL("[document-url]"));
            clp.process(a);

            int args = clp.positionalArguments().size();
            if (args < 3 || args > 4)
                clp.showHelp(1);

            QMap<QString, int> stdRedirections;
            if (clp.isSet(qSL("attach-stdin")))
                stdRedirections[qSL("in")] = 0;
            if (clp.isSet(qSL("attach-stdout")))
                stdRedirections[qSL("out")] = 1;
            if (clp.isSet(qSL("attach-stderr")))
                stdRedirections[qSL("err")] = 2;

            debugApplication(clp.positionalArguments().at(1), clp.positionalArguments().at(2),
                             stdRedirections, args == 3 ? clp.positionalArguments().at(2) : QString());
            break;
        }
        case StopApplication:
            clp.addPositionalArgument(qSL("application-id"), qSL("The id of an installed application."));
            clp.process(a);

            if (clp.positionalArguments().size() != 2)
                clp.showHelp(1);

            stopApplication(clp.positionalArguments().at(1));
            break;

        case ListApplications:
            clp.process(a);
            listApplications();
            break;

        case ShowApplication:
            clp.addPositionalArgument(qSL("application-id"), qSL("The id of an installed application."));
            clp.process(a);

            if (clp.positionalArguments().size() != 2)
                clp.showHelp(1);

            showApplication(clp.positionalArguments().at(1));
            break;

        case InstallPackage:
            clp.addOption({ { qSL("l"), qSL("location") }, qSL("Set a custom installation location."), qSL("installation-location"), qSL("internal-0") });
            clp.addPositionalArgument(qSL("package"), qSL("The file name of the package; can be - for stdin."));
            clp.process(a);

            if (clp.positionalArguments().size() != 2)
                clp.showHelp(1);

            installPackage(clp.positionalArguments().at(1), clp.value(qSL("l")));
            break;

        case RemovePackage:
            clp.addOption({ { qSL("f"), qSL("force") }, qSL("Force removal of package.") });
            clp.addOption({ { qSL("k"), qSL("keep-documents") }, qSL("Keep the document folder of the application.") });
            clp.addPositionalArgument(qSL("application-id"), qSL("The id of an installed application."));
            clp.process(a);

            if (clp.positionalArguments().size() != 2)
                clp.showHelp(1);

            removePackage(clp.positionalArguments().at(1), clp.isSet(qSL("k")), clp.isSet(qSL("f")));
            break;

        case ListInstallationLocations:
            clp.process(a);
            listInstallationLocations();
            break;

        case ShowInstallationLocation:
            clp.addPositionalArgument(qSL("installation-location"), qSL("The id of an installation location."));
            clp.process(a);

            if (clp.positionalArguments().size() != 2)
                clp.showHelp(1);

            showInstallationLocation(clp.positionalArguments().at(1));
            break;
        }

        int result = a.exec();
        if (a.exception())
            throw *a.exception();
        return result;

    } catch (const Exception &e) {
        fprintf(stderr, "ERROR: %s\n", qPrintable(e.errorString()));
        return 1;
    }
}

void startApplication(const QString &appId, const QMap<QString, int> &stdRedirections, const QString &documentUrl = QString())
{
    dbus.connectToManager();

    QTimer::singleShot(0, [appId, stdRedirections, documentUrl]() {
        QDBusPendingReply<bool> reply;
        if (stdRedirections.isEmpty()) {
            reply = dbus.manager()->startApplication(appId, documentUrl);
        } else {
            UnixFdMap fdMap;
            for (auto it = stdRedirections.cbegin(); it != stdRedirections.cend(); ++it)
                fdMap.insert(it.key(), QDBusUnixFileDescriptor(it.value()));

            reply = dbus.manager()->startApplication(appId, fdMap, documentUrl);
        }

        reply.waitForFinished();
        if (reply.isError())
            throw Exception(Error::IO, "failed to call startApplication via DBus: %1").arg(reply.error().message());

        bool ok = reply.value();
        if (stdRedirections.isEmpty()) {
            qApp->exit(ok ? 0 : 2);
        } else {
            if (!ok)
                qApp->exit(2);
        }
    });
}

void debugApplication(const QString &debugWrapper, const QString &appId, const QMap<QString, int> &stdRedirections, const QString &documentUrl = QString())
{
    dbus.connectToManager();

    QTimer::singleShot(0, [debugWrapper, appId, stdRedirections, documentUrl]() {
        QDBusPendingReply<bool> reply;
        if (stdRedirections.isEmpty()) {
            reply = dbus.manager()->debugApplication(appId, debugWrapper, documentUrl);
        } else {
            UnixFdMap fdMap;
            for (auto it = stdRedirections.cbegin(); it != stdRedirections.cend(); ++it)
                fdMap.insert(it.key(), QDBusUnixFileDescriptor(it.value()));

            reply = dbus.manager()->debugApplication(appId, debugWrapper, fdMap, documentUrl);
        }

        reply.waitForFinished();
        if (reply.isError())
            throw Exception(Error::IO, "failed to call debugApplication via DBus: %1").arg(reply.error().message());

        bool ok = reply.value();
        if (stdRedirections.isEmpty()) {
            qApp->exit(ok ? 0 : 2);
        } else {
            if (!ok)
                qApp->exit(2);
        }
    });
}

void stopApplication(const QString &appId)
{
    dbus.connectToManager();

    QTimer::singleShot(0, [appId]() {
        auto reply = dbus.manager()->stopApplication(appId);
        reply.waitForFinished();
        if (reply.isError())
            throw Exception(Error::IO, "failed to call stopApplication via DBus: %1").arg(reply.error().message());
        qApp->quit();
    });

}

void listApplications()
{
    dbus.connectToManager();

    QTimer::singleShot(0, []() {
        auto reply = dbus.manager()->applicationIds();
        reply.waitForFinished();
        if (reply.isError())
            throw Exception(Error::IO, "failed to call applicationIds via DBus: %1").arg(reply.error().message());

        fprintf(stdout, "%s\n", qPrintable(reply.value().join(qL1C('\n'))));
        qApp->quit();
    });
}

void showApplication(const QString &appId)
{
    dbus.connectToManager();

    QTimer::singleShot(0, [appId]() {
        auto reply = dbus.manager()->get(appId);
        reply.waitForFinished();
        if (reply.isError())
            throw Exception(Error::IO, "failed to get application via DBus: %1").arg(reply.error().message());

        QVariant app = reply.value();
        fprintf(stdout, "%s\n", QtYaml::yamlFromVariantDocuments({ app }).constData());
        qApp->quit();
    });
}

void installPackage(const QString &package, const QString &location) throw(Exception)
{
    QString packageFile = package;

    if (package == qL1S("-")) { // sent via stdin
        bool success = false;

        QTemporaryFile *tf = new QTemporaryFile(qApp);
        QFile in;

        if (tf->open() && in.open(stdin, QIODevice::ReadOnly)) {
            packageFile = tf->fileName();

            while (!in.atEnd() && !tf->error())
                tf->write(in.read(1024 * 1024 * 8));

            success = in.atEnd() && !tf->error();
            tf->flush();
        }

        if (!success)
            throw Exception(Error::IO, "Could not copy from stdin to temporary file %1").arg(package);
    }

    QFileInfo fi(package);
    if (!fi.exists() || !fi.isReadable() || !fi.isFile())
        throw Exception(Error::IO, "Package file is not readable: %1").arg(packageFile);

    fprintf(stdout, "Starting installation of package %s to %s...\n", qPrintable(packageFile), qPrintable(location));

    dbus.connectToManager();
    dbus.connectToInstaller();

    // all the async snippets below need to share these variables
    static QString installationId;
    static QString applicationId;

    // start the package installation

    QTimer::singleShot(0, [location, fi]() {
        auto reply = dbus.installer()->startPackageInstallation(location, fi.absoluteFilePath());
        reply.waitForFinished();
        if (reply.isError())
            throw Exception(Error::IO, "failed to call startPackageInstallation via DBus: %1").arg(reply.error().message());

        installationId = reply.value();
        if (installationId.isEmpty())
            throw Exception(Error::IO, "startPackageInstallation returned an empty taskId");
    });

    // as soon as we have the manifest available: get the app id and acknowledge the installation

    QObject::connect(dbus.installer(), &IoQtApplicationInstallerInterface::taskRequestingInstallationAcknowledge,
                     [](const QString &taskId, const QVariantMap &metadata) {
        if (taskId != installationId)
            return;
        applicationId = metadata.value(qSL("id")).toString();
        if (applicationId.isEmpty())
            throw Exception(Error::IO, "could not find a valid application id in the package - got: %1").arg(applicationId);
        fprintf(stdout, "Acknowledging package installation...\n");
        dbus.installer()->acknowledgePackageInstallation(taskId);
    });

    // on failure: quit

    QObject::connect(dbus.installer(), &IoQtApplicationInstallerInterface::taskFailed,
                     [](const QString &taskId, int errorCode, const QString &errorString) {
        if (taskId != installationId)
            return;
        throw Exception(Error::IO, "failed to install package: %1 (code: %2)").arg(errorString).arg(errorCode);
    });

    // on success

    QObject::connect(dbus.installer(), &IoQtApplicationInstallerInterface::taskFinished,
                     [](const QString &taskId) {
        if (taskId != installationId)
            return;
        fprintf(stdout, "Package installation finished successfully.\n");
        qApp->quit();
    });
}

void removePackage(const QString &applicationId, bool keepDocuments, bool force) throw(Exception)
{
    fprintf(stdout, "Starting removal of package %s...\n", qPrintable(applicationId));

    dbus.connectToManager();
    dbus.connectToInstaller();

    // all the async snippets below need to share these variables
    static QString installationId;

    // start the package installation

    QTimer::singleShot(0, [applicationId, keepDocuments, force]() {
        auto reply = dbus.installer()->removePackage(applicationId, keepDocuments, force);
        reply.waitForFinished();
        if (reply.isError())
            throw Exception(Error::IO, "failed to call removePackage via DBus: %1").arg(reply.error().message());

        installationId = reply.value();
        if (installationId.isEmpty())
            throw Exception(Error::IO, "removePackage returned an empty taskId");
    });

    // on failure: quit

    QObject::connect(dbus.installer(), &IoQtApplicationInstallerInterface::taskFailed,
                     [](const QString &taskId, int errorCode, const QString &errorString) {
        if (taskId != installationId)
            return;
        throw Exception(Error::IO, "failed to remove package: %1 (code: %2)").arg(errorString).arg(errorCode);
    });

    // on success

    QObject::connect(dbus.installer(), &IoQtApplicationInstallerInterface::taskFinished,
                     [](const QString &taskId) {
        if (taskId != installationId)
            return;
        fprintf(stdout, "Package removal finished successfully.\n");
        qApp->quit();
    });
}

void listInstallationLocations()
{
    dbus.connectToInstaller();

    QTimer::singleShot(0, []() {
        auto reply = dbus.installer()->installationLocationIds();
        reply.waitForFinished();
        if (reply.isError())
            throw Exception(Error::IO, "failed to call installationLocationIds via DBus: %1").arg(reply.error().message());

        fprintf(stdout, "%s\n", qPrintable(reply.value().join(qL1C('\n'))));
        qApp->quit();
    });
}

void showInstallationLocation(const QString &location)
{
    dbus.connectToInstaller();

    QTimer::singleShot(0, [location]() {
        auto reply = dbus.installer()->getInstallationLocation(location);
        reply.waitForFinished();
        if (reply.isError())
            throw Exception(Error::IO, "failed to call getInstallationLocation via DBus: %1").arg(reply.error().message());

        QVariant app = reply.value();
        fprintf(stdout, "%s\n", QtYaml::yamlFromVariantDocuments({ app }).constData());
        qApp->quit();
    });
}

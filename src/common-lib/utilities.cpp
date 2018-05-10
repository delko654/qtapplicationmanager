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

#ifdef _WIN32
// needed for QueryFullProcessImageNameW
#  define WINVER _WIN32_WINNT_VISTA
#  define _WIN32_WINNT _WIN32_WINNT_VISTA
#endif

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QCoreApplication>
#include <QNetworkInterface>
#include <QPluginLoader>

#include "utilities.h"
#include "exception.h"
#include "unixsignalhandler.h"

#include <errno.h>

#if defined(Q_OS_UNIX)
#  include <unistd.h>
#  include <sys/ioctl.h>
#  include <termios.h>
#  include <signal.h>
#endif
#if defined(Q_OS_WIN)
#  include <QThread>
#  include <windows.h>
#  include <io.h>
#  include <tlhelp32.h>
#elif defined(Q_OS_OSX)
#  include <unistd.h>
#  include <sys/mount.h>
#  include <sys/statvfs.h>
#  include <sys/sysctl.h>
#  include <libproc.h>
#else
#  include <stdio.h>
#  include <mntent.h>
#  include <sys/stat.h>
#  if defined(Q_OS_ANDROID)
#    include <sys/vfs.h>
#    define statvfs statfs
#  else
#    include <sys/statvfs.h>
#  endif
#  include <qplatformdefs.h>

// the process environment from the C library
extern char **environ;
#endif

QT_BEGIN_NAMESPACE_AM

/*! \internal
    Check a YAML document against the "standard" AM header.
    If \a numberOfDocuments is positive, the number of docs need to match exactly. If it is
    negative, the \a numberOfDocuments is taken as the required minimum amount of documents.

*/
void checkYamlFormat(const QVector<QVariant> &docs, int numberOfDocuments,
                     const QVector<QByteArray> &formatTypes, int formatVersion) Q_DECL_NOEXCEPT_EXPR(false)
{
    int actualSize = docs.size();
    QByteArray actualFormatType;
    int actualFormatVersion = 0;

    if (actualSize >= 1) {
        const auto map = docs.constFirst().toMap();
        actualFormatType = map.value(qSL("formatType")).toString().toUtf8();
        actualFormatVersion = map.value(qSL("formatVersion")).toInt(0);
    }

    if (numberOfDocuments < 0) {
        if (actualSize < numberOfDocuments) {
            throw Exception("wrong number of YAML documents: expected at least %1, got %2")
                .arg(-numberOfDocuments).arg(actualSize);
        }
    } else {
        if (actualSize != numberOfDocuments) {
            throw Exception("wrong number of YAML documents: expected %1, got %2")
                .arg(numberOfDocuments).arg(actualSize);
        }
    }
    if (!formatTypes.contains(actualFormatType)) {
        throw Exception("wrong formatType header: expected %1, got %2")
            .arg(QString::fromUtf8(formatTypes.toList().join(", or ")), QString::fromUtf8(actualFormatType));
    }
    if (actualFormatVersion != formatVersion) {
        throw Exception("wrong formatVersion header: expected %1, got %2")
                .arg(formatVersion).arg(actualFormatVersion);
    }
}


QMultiMap<QString, QString> mountedDirectories()
{
    QMultiMap<QString, QString> result;
#if defined(Q_OS_WIN)
    return result; // no mounts on Windows

#elif defined(Q_OS_OSX)
    struct statfs *sfs = nullptr;
    int count = getmntinfo(&sfs, MNT_NOWAIT);

    for (int i = 0; i < count; ++i, ++sfs) {
        result.insertMulti(QString::fromLocal8Bit(sfs->f_mntonname), QString::fromLocal8Bit(sfs->f_mntfromname));
    }
#else
    FILE *pm = fopen("/proc/self/mounts", "r");
    if (!pm)
        return result;

#  if defined(Q_OS_ANDROID)
    while (struct mntent *mntPtr = getmntent(pm)) {
        result.insertMulti(QString::fromLocal8Bit(mntPtr->mnt_dir),
                           QString::fromLocal8Bit(mntPtr->mnt_fsname));
    }
#  else
    int pathMax = pathconf("/", _PC_PATH_MAX) * 2 + 1024;  // quite big, but better be safe than sorry
    QScopedArrayPointer<char> strBuf(new char[pathMax]);
    struct mntent mntBuf;

    while (getmntent_r(pm, &mntBuf, strBuf.data(), pathMax - 1)) {
        result.insertMulti(QString::fromLocal8Bit(mntBuf.mnt_dir),
                           QString::fromLocal8Bit(mntBuf.mnt_fsname));
    }
#  endif
    fclose(pm);
#endif

    return result;
}

bool safeRemove(const QString &path, RecursiveOperationType type)
{
   static const QFileDevice::Permissions fullAccess =
           QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
           QFileDevice::ReadUser  | QFileDevice::WriteUser  | QFileDevice::ExeUser  |
           QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
           QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

   switch (type) {
   case RecursiveOperationType::EnterDirectory:
       return QFile::setPermissions(path, fullAccess);

   case RecursiveOperationType::LeaveDirectory: {
        // QDir cannot delete the directory it is pointing to
       QDir dir(path);
       QString dirName = dir.dirName();
       return dir.cdUp() && dir.rmdir(dirName);
   }
   case RecursiveOperationType::File:
       return QFile::remove(path);
   }
   return false;
}

void getOutputInformation(bool *ansiColorSupport, bool *runningInCreator, int *consoleWidth)
{
    static bool ansiColorSupportDetected = false;
    static bool runningInCreatorDetected = false;
    static QAtomicInteger<int> consoleWidthCached(0);
    static int consoleWidthCalculated = -1;
    static bool once = false;

    if (!once) {
        enum { ColorAuto, ColorOff, ColorOn } forceColor = ColorAuto;
        QByteArray forceColorOutput = qgetenv("AM_FORCE_COLOR_OUTPUT");
        if (forceColorOutput == "off" || forceColorOutput == "0")
            forceColor = ColorOff;
        else if (forceColorOutput == "on" || forceColorOutput == "1")
            forceColor = ColorOn;

#if defined(Q_OS_UNIX)
        if (::isatty(STDERR_FILENO))
            ansiColorSupportDetected = true;

#elif defined(Q_OS_WIN)
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        if (h != INVALID_HANDLE_VALUE) {
            if (QSysInfo::windowsVersion() >= QSysInfo::WV_10_0) {
                // enable ANSI mode on Windows 10
                DWORD mode = 0;
                if (GetConsoleMode(h, &mode)) {
                    mode |= 0x04;
                    if (SetConsoleMode(h, mode))
                        ansiColorSupportDetected = true;
                }
            }
        }
#endif

        qint64 pid = QCoreApplication::applicationPid();
        forever {
            pid = getParentPid(pid);
            if (pid <= 1)
                break;

#if defined(Q_OS_LINUX)
            static QString checkCreator = qSL("/proc/%1/exe");
            QFileInfo fi(checkCreator.arg(pid));
            if (fi.readLink().contains(qSL("qtcreator"))) {
                runningInCreatorDetected = true;
                break;
            }
#elif defined(Q_OS_OSX)
            static char buffer[PROC_PIDPATHINFO_MAXSIZE + 1];
            int len = proc_pidpath(pid, buffer, sizeof(buffer) - 1);
            if ((len > 0) && QByteArray::fromRawData(buffer, len).contains("Qt Creator")) {
                runningInCreatorDetected = true;
                break;
            }
#elif defined(Q_OS_WIN)
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
            if (hProcess) {
                wchar_t exeName[1024] = { 0 };
                DWORD exeNameSize = sizeof(exeName) - 1;
                if (QueryFullProcessImageNameW(hProcess, 0, exeName, &exeNameSize)) {
                    if (QString::fromWCharArray(exeName, exeNameSize).contains(qSL("qtcreator.exe")))
                        runningInCreatorDetected = true;
                }
            }
#endif
        }

        if (forceColor != ColorAuto)
            ansiColorSupportDetected = (forceColor == ColorOn);
        else if (!ansiColorSupportDetected)
            ansiColorSupportDetected = runningInCreatorDetected;

#if defined(Q_OS_UNIX) && defined(SIGWINCH)
        UnixSignalHandler::instance()->install(UnixSignalHandler::RawSignalHandler, SIGWINCH, [](int) {
            consoleWidthCached = 0;
        });
#elif defined(Q_OS_WIN)
        class ConsoleThread : public QThread
        {
        public:
            ConsoleThread(QObject *parent)
                : QThread(parent)
            { }
        protected:
            void run() override
            {
                HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
                DWORD mode = 0;
                if (!GetConsoleMode(h, &mode))
                    return;
                if (!SetConsoleMode(h, mode | ENABLE_WINDOW_INPUT))
                    return;

                INPUT_RECORD ir;
                DWORD irRead = 0;
                while (ReadConsoleInputW(h, &ir, 1, &irRead)) {
                    if ((irRead == 1) && (ir.EventType == WINDOW_BUFFER_SIZE_EVENT))
                        consoleWidthCached = 0;
                }
            }
        };
        (new ConsoleThread(qApp))->start();
#endif // Q_OS_WIN
        once = true;
    }

    if (ansiColorSupport)
        *ansiColorSupport = ansiColorSupportDetected;
    if (runningInCreator)
        *runningInCreator = runningInCreatorDetected;
    if (consoleWidth) {
        if (!consoleWidthCached) {
            consoleWidthCached = 1;
            consoleWidthCalculated = -1;
#if defined(Q_OS_UNIX)
            if (::isatty(STDERR_FILENO)) {
                struct ::winsize ws;
                if ((::ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0) && (ws.ws_col > 0))
                    consoleWidthCalculated = ws.ws_col;
            }
#elif defined(Q_OS_WIN)
            HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
            if (h != INVALID_HANDLE_VALUE && h != NULL) {
                CONSOLE_SCREEN_BUFFER_INFO csbi;
                if (GetConsoleScreenBufferInfo(h, &csbi))
                    consoleWidthCalculated = csbi.dwSize.X;
            }
#endif
        }
        if ((consoleWidthCalculated <= 0) && runningInCreatorDetected)
            *consoleWidth = 120;
        else
            *consoleWidth = consoleWidthCalculated;
    }
}

qint64 getParentPid(qint64 pid)
{
    qint64 ppid = 0;

#if defined(Q_OS_LINUX)
    static QString proc = qSL("/proc/%1/stat");
    QFile f(proc.arg(pid));
    if (f.open(QIODevice::ReadOnly)) {
        // we need just the 4th field, but the 2nd is the binary name, which could be long
        QByteArray ba = f.read(512);
        // the binary name could contain ')' and/or ' ' and the kernel escapes neither...
        int pos = ba.lastIndexOf(')');
        if (pos > 0 && ba.length() > (pos + 5))
            ppid = strtoll(ba.constData() + pos + 4, nullptr, 10);
    }

#elif defined(Q_OS_OSX)
    int mibNames[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (pid_t) pid };
    size_t procInfoSize;

    if (sysctl(mibNames, sizeof(mibNames) / sizeof(mibNames[0]), nullptr, &procInfoSize, nullptr, 0) == 0) {
        kinfo_proc *procInfo = (kinfo_proc *) malloc(procInfoSize);

        if (sysctl(mibNames, sizeof(mibNames) / sizeof(mibNames[0]), procInfo, &procInfoSize, nullptr, 0) == 0)
            ppid = procInfo->kp_eproc.e_ppid;
        free(procInfo);
    }

#elif defined(Q_OS_WIN)
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(pe32);
    HANDLE hProcess = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, pid);
    if (Process32First(hProcess, &pe32)) {
        do {
            if (pe32.th32ProcessID == pid) {
                ppid = pe32.th32ParentProcessID;
                break;
            }
        } while (Process32Next(hProcess, &pe32));
    }
    CloseHandle(hProcess);
#else
    Q_UNUSED(pid)
#endif
    return ppid;
}

int timeoutFactor()
{
    static int tf = 0;
    if (!tf) {
        tf = qMax(1, qEnvironmentVariableIntValue("AM_TIMEOUT_FACTOR"));
        if (tf > 1)
            qInfo() << "All timeouts are multiplied by" << tf << "(changed by (un)setting $AM_TIMEOUT_FACTOR)";
    }
    return tf;
}

bool recursiveOperation(const QString &path, const std::function<bool (const QString &, RecursiveOperationType)> &operation)
{
    QFileInfo pathInfo(path);

    if (pathInfo.isDir()) {
        if (!operation(path, RecursiveOperationType::EnterDirectory))
            return false;

        QDirIterator dit(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (dit.hasNext()) {
            dit.next();
            QFileInfo ditInfo = dit.fileInfo();

            if (ditInfo.isDir()) {
                if (!recursiveOperation(ditInfo.filePath(), operation))
                    return false;
            } else {
                if (!operation(ditInfo.filePath(), RecursiveOperationType::File))
                    return false;
            }
        }
        return operation(path, RecursiveOperationType::LeaveDirectory);
    } else {
        return operation(path, RecursiveOperationType::File);
    }
}

bool recursiveOperation(const QByteArray &path, const std::function<bool (const QString &, RecursiveOperationType)> &operation)
{
    return recursiveOperation(QString::fromLocal8Bit(path), operation);
}

bool recursiveOperation(const QDir &path, const std::function<bool (const QString &, RecursiveOperationType)> &operation)
{
    return recursiveOperation(path.absolutePath(), operation);
}

QVector<QObject *> loadPlugins_helper(const char *type, const QStringList &files, const char *iid) Q_DECL_NOEXCEPT_EXPR(false)
{
    QVector<QObject *> interfaces;

    try {
        for (const QString &pluginFilePath : files) {
            QPluginLoader pluginLoader(pluginFilePath);
            if (Q_UNLIKELY(!pluginLoader.load())) {
                throw Exception("could not load %1 plugin %2: %3")
                        .arg(qL1S(type)).arg(pluginFilePath, pluginLoader.errorString());
            }
            QScopedPointer<QObject> iface(pluginLoader.instance());
            if (Q_UNLIKELY(!iface || !iface->qt_metacast(iid))) {
                throw Exception("could not get an instance of '%1' from the %2 plugin %3")
                        .arg(qL1S(iid)).arg(qL1S(type)).arg(pluginFilePath);
            }
            interfaces << iface.take();
        }
    } catch (const Exception &) {
        qDeleteAll(interfaces);
        throw;
    }
    return interfaces;
}

QT_END_NAMESPACE_AM

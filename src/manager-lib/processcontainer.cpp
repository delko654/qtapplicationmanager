/****************************************************************************
**
** Copyright (C) 2016 Pelagicore AG
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

#include "global.h"
#include "containerfactory.h"
#include "application.h"
#include "processcontainer.h"

#if defined(Q_OS_UNIX)
#  include <csignal>
#  include <unistd.h>
#  include <fcntl.h>
#endif

QT_BEGIN_NAMESPACE_AM

HostProcess::HostProcess()
{
    m_process.setProcessChannelMode(QProcess::ForwardedChannels);
    m_process.setInputChannelMode(QProcess::ForwardedInputChannel);
}

void HostProcess::start(const QString &program, const QStringList &arguments)
{
    connect(&m_process, &QProcess::started, this, &HostProcess::started);
    connect(&m_process, static_cast<void (QProcess::*)(QProcess::ProcessError)>(&QProcess::error),
            this, &HostProcess::errorOccured);
    connect(&m_process, static_cast<void (QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),
            this, &HostProcess::finished);
    connect(&m_process, &QProcess::stateChanged, this, &HostProcess::stateChanged);

    m_process.start(program, arguments);
}

void HostProcess::setWorkingDirectory(const QString &dir)
{
    m_process.setWorkingDirectory(dir);
}

void HostProcess::setProcessEnvironment(const QProcessEnvironment &environment)
{
    m_process.setProcessEnvironment(environment);
}

void HostProcess::kill()
{
    m_process.kill();
}

void HostProcess::terminate()
{
    m_process.terminate();
}

qint64 HostProcess::processId() const
{
    return m_process.processId();
}

QProcess::ProcessState HostProcess::state() const
{
    return m_process.state();
}

void HostProcess::setRedirections(const QVector<int> &stdRedirections)
{
    m_process.m_stdRedirections = stdRedirections;

#if defined(Q_OS_UNIX)
    for (int fd : qAsConst(m_process.m_stdRedirections)) {
        if (fd < 0)
            continue;
        int flags = fcntl(fd, F_GETFD);
        if (flags & FD_CLOEXEC)
            fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);
    }
#endif
}

void HostProcess::setStopBeforeExec(bool stopBeforeExec)
{
    m_process.m_stopBeforeExec = stopBeforeExec;
}



ProcessContainer::ProcessContainer(ProcessContainerManager *manager)
    : AbstractContainer(manager)
{ }

ProcessContainer::ProcessContainer(const ContainerDebugWrapper &debugWrapper, ProcessContainerManager *manager)
    : AbstractContainer(manager)
    , m_useDebugWrapper(true)
    , m_debugWrapper(debugWrapper)
{ }

ProcessContainer::~ProcessContainer()
{ }

QString ProcessContainer::controlGroup() const
{
    return m_currentControlGroup;
}

bool ProcessContainer::setControlGroup(const QString &groupName)
{
    if (groupName == m_currentControlGroup)
        return true;

    QVariantMap map = m_manager->configuration().value(qSL("controlGroups")).toMap();
    auto git = map.constFind(groupName);
    if (git != map.constEnd()) {
        QVariantMap mapping = (*git).toMap();
        QByteArray pidString = QByteArray::number(m_process->processId());
        pidString.append('\n');

        for (auto it = mapping.cbegin(); it != mapping.cend(); ++it) {
            const QString &resource = it.key();
            const QString &userclass = it.value().toString();

            //qWarning() << "Setting cgroup for" << m_program << ", pid" << m_process->processId() << ":" << resource << "->" << userclass;

            QString file = QString(qSL("/sys/fs/cgroup/%1/%2/cgroup.procs")).arg(resource, userclass);
            QFile f(file);
            bool ok = f.open(QFile::WriteOnly);
            ok = ok && (f.write(pidString) == pidString.size());

            if (!ok) {
                qWarning() << "Failed setting cgroup for" << m_program << ", pid" << m_process->processId() << ":" << resource << "->" << userclass;
                return false;
            }
        }
        m_currentControlGroup = groupName;
        return true;
    }
    return false;
}

bool ProcessContainer::isReady()
{
    return true;
}

AbstractContainerProcess *ProcessContainer::start(const QStringList &arguments, const QProcessEnvironment &environment)
{
    if (m_process) {
        qWarning() << "Process" << m_program << "is already started and cannot be started again";
        return nullptr;
    }
    if (!QFile::exists(m_program))
        return nullptr;

    QProcessEnvironment completeEnv = environment;
    if (completeEnv.isEmpty())
        completeEnv = QProcessEnvironment::systemEnvironment();

    HostProcess *process = new HostProcess();
    process->setWorkingDirectory(m_baseDirectory);
    process->setProcessEnvironment(completeEnv);
    process->setStopBeforeExec(configuration().value(qSL("stopBeforeExec")).toBool());

    QString command = m_program;
    QStringList args = arguments;

    if (m_useDebugWrapper) {
        m_debugWrapper.resolveParameters(m_program, arguments);
        process->setRedirections(m_debugWrapper.stdRedirections());

        command = m_debugWrapper.command().at(0);
        args = m_debugWrapper.command().mid(1);
    }
    qCDebug(LogSystem) << "Running command:" << command << args;

    process->start(command, args);
    m_process = process;

    setControlGroup(configuration().value(qSL("defaultControlGroup")).toString());
    return process;
}

ProcessContainerManager::ProcessContainerManager(QObject *parent)
    : AbstractContainerManager(defaultIdentifier(), parent)
{ }

ProcessContainerManager::ProcessContainerManager(const QString &id, QObject *parent)
    : AbstractContainerManager(id, parent)
{ }

QString ProcessContainerManager::defaultIdentifier()
{
    return qSL("process");
}

bool ProcessContainerManager::supportsQuickLaunch() const
{
    return true;
}

AbstractContainer *ProcessContainerManager::create()
{
    return new ProcessContainer(this);
}

AbstractContainer *ProcessContainerManager::create(const ContainerDebugWrapper &debugWrapper)
{
    return new ProcessContainer(debugWrapper, this);
}

void HostProcess::MyQProcess::setupChildProcess()
{
#if defined(Q_OS_UNIX)
    if (m_stopBeforeExec) {
        fprintf(stderr, "\n*** a 'process' container was started in stopped state ***\nthe process is suspended via SIGSTOP and you can attach a debugger to it via\n\n   gdb -p %d\n\n", getpid());
        raise(SIGSTOP);
    }
    for (int i = 0; i < 3; ++i) {
        int fd = m_stdRedirections.value(i, -1);
        if (fd >= 0)
            dup2(fd, i);
    }
#endif
}

QT_END_NAMESPACE_AM


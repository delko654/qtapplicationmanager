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

#pragma once

#include <qglobal.h>

#if defined(AM_MULTI_PROCESS)

#include <QtPlugin>
#include <QProcess>
#include <QVector>

#include <QtAppManManager/abstractruntime.h>
#include <QtAppManManager/abstractcontainer.h>

#define AM_NATIVE_RUNTIME_AVAILABLE

QT_FORWARD_DECLARE_CLASS(QDBusConnection)
QT_FORWARD_DECLARE_CLASS(QDBusServer)

QT_BEGIN_NAMESPACE_AM

class Notification;
class NativeRuntime;
class NativeRuntimeInterface;
class NativeRuntimeApplicationInterface;
class ApplicationIPCInterface;

class NativeRuntimeManager : public AbstractRuntimeManager
{
    Q_OBJECT
public:
    explicit NativeRuntimeManager(QObject *parent = nullptr);
    explicit NativeRuntimeManager(const QString &id, QObject *parent = nullptr);

    static QString defaultIdentifier();
    bool supportsQuickLaunch() const override;

    AbstractRuntime *create(AbstractContainer *container, const Application *app) override;
};

class NativeRuntime : public AbstractRuntime
{
    Q_OBJECT

public:
    ~NativeRuntime();

    bool isQuickLauncher() const override;
    bool attachApplicationToQuickLauncher(const Application *app) override;

    qint64 applicationProcessId() const override;
    void openDocument(const QString &document, const QString &mimeType) override;
    void setSlowAnimations(bool slow) override;

    bool sendNotificationUpdate(Notification *n);

public slots:
    bool start() override;
    void stop(bool forceKill = false) override;

signals:
    void aboutToStop(); // used for the ApplicationInterface
    void interfaceCreated(const QString &interfaceName);

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void onDBusPeerConnection(const QDBusConnection &connection);
    void onApplicationFinishedInitialization();

protected:
    explicit NativeRuntime(AbstractContainer *container, const Application *app, NativeRuntimeManager *parent);

private:
    bool initialize();
    void shutdown(int exitCode, QProcess::ExitStatus status);
    void registerExtensionInterfaces();
    QDBusServer *applicationInterfaceServer() const;

    bool m_isQuickLauncher;
    bool m_needsLauncher;

    QString m_document;
    QString m_mimeType;
    bool m_launchWhenReady = false;
    bool m_applicationInterfaceConnected = false;
    bool m_dbusConnection = false;
    QString m_dbusConnectionName;

    QList<ApplicationIPCInterface *> m_applicationIPCInterfaces;
    NativeRuntimeApplicationInterface *m_applicationInterface = nullptr;
    NativeRuntimeInterface *m_runtimeInterface = nullptr;
    AbstractContainerProcess *m_process = nullptr;
    QDBusServer *m_applicationInterfaceServer;
    bool m_slowAnimations = false;
    QVariantMap m_openGLConfiguration;

    friend class NativeRuntimeManager;
};

QT_END_NAMESPACE_AM

#endif //defined(QT_DBUS_LIB)

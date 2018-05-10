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

#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QCoreApplication>
#include <QTimer>

#if !defined(AM_HEADLESS)
#  include <QQuickView>

#  include "fakeapplicationmanagerwindow.h"
#  include "inprocesssurfaceitem.h"
#endif

#include "logging.h"
#include "application.h"
#include "qmlinprocessruntime.h"
#include "qmlinprocessapplicationinterface.h"
#include "abstractcontainer.h"
#include "global.h"
#include "utilities.h"
#include "runtimefactory.h"
#include "qml-utilities.h"

#if defined(Q_OS_UNIX)
#  include <signal.h>
#endif

QT_BEGIN_NAMESPACE_AM


const char *QmlInProcessRuntime::s_runtimeKey = "_am_runtime";


QmlInProcessRuntime::QmlInProcessRuntime(const Application *app, QmlInProcessRuntimeManager *manager)
    : AbstractRuntime(nullptr, app, manager)
{ }

QmlInProcessRuntime::~QmlInProcessRuntime()
{
#if !defined(AM_HEADLESS)
    // if there is still a window present at this point, fire the 'closing' signal (probably) again,
    // because it's still the duty of WindowManager together with qml-ui to free and delete this item!!
    for (int i = m_surfaces.size(); i; --i)
        emit inProcessSurfaceItemClosing(m_surfaces.at(i-1));
#endif
}

bool QmlInProcessRuntime::start()
{
#if !defined(AM_HEADLESS)
    Q_ASSERT(!m_rootObject);
#endif
    setState(Startup);

    if (!m_inProcessQmlEngine)
        return false;

    if (!m_app) {
        qCCritical(LogSystem) << "tried to start without an app object";
        return false;
    }

    if (m_app->runtimeParameters().value(qSL("loadDummyData")).toBool()) {
        qCDebug(LogSystem) << "Loading dummy-data";
        loadQmlDummyDataFiles(m_inProcessQmlEngine, QFileInfo(m_app->absoluteCodeFilePath()).path());
    }

    const QStringList importPaths = variantToStringList(configuration().value(qSL("importPaths")))
                                  + variantToStringList(m_app->runtimeParameters().value(qSL("importPaths")));
    if (!importPaths.isEmpty()) {
        const QString codeDir = m_app->codeDir() + QDir::separator();
        for (const QString &path : importPaths)
            m_inProcessQmlEngine->addImportPath(QFileInfo(path).isRelative() ? codeDir + path : path);

        qCDebug(LogSystem) << "Updated Qml import paths:" << m_inProcessQmlEngine->importPathList();
    }

    m_componentError = false;
    QQmlComponent *component = new QQmlComponent(m_inProcessQmlEngine, m_app->absoluteCodeFilePath());

    if (!component->isReady()) {
        qCDebug(LogSystem) << "qml-file (" << m_app->absoluteCodeFilePath() << "): component not ready:\n" << component->errorString();
        return false;
    }

    // We are running each application in it's own, separate Qml context.
    // This way, we can export an unique ApplicationInterface object for each app
    QQmlContext *appContext = new QQmlContext(m_inProcessQmlEngine->rootContext());
    m_applicationIf = new QmlInProcessApplicationInterface(this);
    appContext->setContextProperty(qSL("ApplicationInterface"), m_applicationIf);
    connect(m_applicationIf, &QmlInProcessApplicationInterface::quitAcknowledged,
            this, [this]() { finish(0, QProcess::NormalExit); });

    if (appContext->setProperty(s_runtimeKey, QVariant::fromValue(this)))
        qCritical() << "Could not set" << s_runtimeKey << "property in QML context";

    QObject *obj = component->beginCreate(appContext);

    QTimer::singleShot(0, this, [component, appContext, obj, this]() {
        component->completeCreate();
        if (!obj || m_componentError) {
            qCCritical(LogSystem) << "could not load" << m_app->absoluteCodeFilePath() << ": no root object";
            delete obj;
            delete appContext;
            delete m_applicationIf;
            m_applicationIf = nullptr;
            finish(3, QProcess::NormalExit);
        } else {
#if !defined(AM_HEADLESS)
            if (!qobject_cast<FakeApplicationManagerWindow*>(obj)) {
                QQuickItem *item = qobject_cast<QQuickItem*>(obj);
                if (item)
                    addWindow(item);
            }
            m_rootObject = obj;
#endif
            if (!m_document.isEmpty())
                openDocument(m_document, QString());
            setState(Active);
        }
        delete component;
    });
    return true;
}

void QmlInProcessRuntime::stop(bool forceKill)
{
    setState(Shutdown);
    emit aboutToStop();

#if !defined(AM_HEADLESS)
    for (int i = m_surfaces.size(); i; --i)
        emit inProcessSurfaceItemClosing(m_surfaces.at(i-1));

    if (m_surfaces.isEmpty()) {
        delete m_rootObject;
        m_rootObject = nullptr;
    }
#endif

    if (forceKill) {
#if defined(Q_OS_UNIX)
        int exitCode = SIGKILL;
#else
        int exitCode = 0;
#endif
        finish(exitCode, QProcess::CrashExit);
        return;
    }

    bool ok;
    int qt = configuration().value(qSL("quitTime")).toInt(&ok);
    if (!ok || qt < 0)
        qt = 250;
    QTimer::singleShot(qt, this, [this]() {
#if defined(Q_OS_UNIX)
        int exitCode = SIGTERM;
#else
        int exitCode = 0;
#endif
        finish(exitCode, QProcess::CrashExit);
    });
}

void QmlInProcessRuntime::finish(int exitCode, QProcess::ExitStatus status)
{
    QTimer::singleShot(0, this, [this, exitCode, status]() {
        qCDebug(LogSystem) << "QmlInProcessRuntime (id:" << (m_app ? m_app->id() : qSL("(none)"))
                           << ") exited with code:" << exitCode << "status:" << status;
        emit finished(exitCode, status);
        setState(Inactive);
#if !defined(AM_HEADLESS)
        if (m_surfaces.isEmpty())
            deleteLater();
#else
        deleteLater();
#endif
    });
}

#if !defined(AM_HEADLESS)

void QmlInProcessRuntime::inProcessSurfaceItemReleased(QQuickItem *surface)
{
    // TODO: Take a snapshot of the last window frame and use this for potential systemUI animations.
    //       Stop the application (delete its object hierarchy) immediately and remove this workaround.
    m_surfaces.removeOne(surface);
    if (state() != Active && m_surfaces.isEmpty()) {
        delete m_rootObject;
        m_rootObject = nullptr;
        if (state() == Inactive)
            deleteLater();
    }
}

void QmlInProcessRuntime::onWindowClose()
{
    QQuickItem* surface = reinterpret_cast<QQuickItem*>(sender()); // reinterpret_cast because the object might be broken down already!
    Q_ASSERT(surface && m_surfaces.contains(surface));

    emit inProcessSurfaceItemClosing(surface);
}

void QmlInProcessRuntime::onWindowDestroyed()
{
    QObject* sndr = sender();
    m_surfaces.removeAll(reinterpret_cast<QQuickItem*>(sndr)); // reinterpret_cast because the object might be broken down already!
    if (m_rootObject == sndr)
        m_rootObject = nullptr;
}

void QmlInProcessRuntime::onEnableFullscreen()
{
    FakeApplicationManagerWindow *surface = qobject_cast<FakeApplicationManagerWindow *>(sender());

    emit inProcessSurfaceItemFullscreenChanging(surface, true);
}

void QmlInProcessRuntime::onDisableFullscreen()
{
    FakeApplicationManagerWindow *surface = qobject_cast<FakeApplicationManagerWindow *>(sender());

    emit inProcessSurfaceItemFullscreenChanging(surface, false);
}

void QmlInProcessRuntime::addWindow(QQuickItem *window)
{
    // Below check is only needed if the root element is a QtObject.
    // It should be possible to remove this, once proper visible handling is in place.
    if (state() != Inactive && state() != Shutdown) {
        auto famw = qobject_cast<FakeApplicationManagerWindow *>(window);
        QQuickItem *surface = famw ? famw->m_surfaceItem : window;

        if (!m_surfaces.contains(surface)) {
            if (famw) {
                surface = new InProcessSurfaceItem(famw);
                connect(famw, &FakeApplicationManagerWindow::fakeFullScreenSignal, this, &QmlInProcessRuntime::onEnableFullscreen);
                connect(famw, &FakeApplicationManagerWindow::fakeNoFullScreenSignal, this, &QmlInProcessRuntime::onDisableFullscreen);
                connect(famw, &FakeApplicationManagerWindow::fakeCloseSignal, this, &QmlInProcessRuntime::onWindowClose);
                connect(famw, &QObject::destroyed, this, &QmlInProcessRuntime::onWindowDestroyed);
            }
            m_surfaces.append(surface);
        }

        emit inProcessSurfaceItemReady(surface);
    }
}

void QmlInProcessRuntime::removeWindow(QQuickItem *window)
{
    auto famw = qobject_cast<FakeApplicationManagerWindow *>(window);
    QQuickItem *surface = famw ? famw->m_surfaceItem : window;
    if (m_surfaces.removeOne(surface))
        emit inProcessSurfaceItemClosing(surface);
}

#endif // !AM_HEADLESS

void QmlInProcessRuntime::openDocument(const QString &document, const QString &mimeType)
{
    m_document = document;
    if (m_applicationIf)
        emit m_applicationIf->openDocument(document, mimeType);
}

qint64 QmlInProcessRuntime::applicationProcessId() const
{
    return QCoreApplication::applicationPid();
}


QmlInProcessRuntimeManager::QmlInProcessRuntimeManager(QObject *parent)
    : AbstractRuntimeManager(defaultIdentifier(), parent)
{ }

QmlInProcessRuntimeManager::QmlInProcessRuntimeManager(const QString &id, QObject *parent)
    : AbstractRuntimeManager(id, parent)
{ }

QString QmlInProcessRuntimeManager::defaultIdentifier()
{
    return qSL("qml-inprocess");
}

bool QmlInProcessRuntimeManager::inProcess() const
{
    return true;
}

AbstractRuntime *QmlInProcessRuntimeManager::create(AbstractContainer *container, const Application *app)
{
    if (container) {
        delete container;
        return nullptr;
    }
    return new QmlInProcessRuntime(app, this);
}

QT_END_NAMESPACE_AM

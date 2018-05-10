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

#include "crashhandler.h"
#include "global.h"

#if !defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)

QT_BEGIN_NAMESPACE_AM

void CrashHandler::setCrashActionConfiguration(const QVariantMap &config)
{
    Q_UNUSED(config)
}

QT_END_NAMESPACE_AM

#else

#include <cxxabi.h>
#include <execinfo.h>
#include <setjmp.h>
#include <signal.h>
#include <inttypes.h>

#if defined(AM_USE_LIBBACKTRACE)
#  include <libbacktrace/backtrace.h>
#  include <libbacktrace/backtrace-supported.h>
#endif

#include "unixsignalhandler.h"
#include "utilities.h"
#include "processtitle.h"

QT_BEGIN_NAMESPACE_AM

static bool printBacktrace;
static bool useAnsiColor;
static bool dumpCore;
static int waitForGdbAttach;

static char *demangleBuffer;
static size_t demangleBufferSize;

// this will make it run before all other static constructor functions
static void initBacktrace() __attribute__((constructor(101)));

static void crashHandler(const char *why, int stackFramesToIgnore) __attribute__((noreturn));

void CrashHandler::setCrashActionConfiguration(const QVariantMap &config)
{
    printBacktrace = config.value(qSL("printBacktrace"), printBacktrace).toBool();
    waitForGdbAttach = config.value(qSL("waitForGdbAttach"), waitForGdbAttach).toInt() * timeoutFactor();
    dumpCore = config.value(qSL("dumpCore"), dumpCore).toBool();
}


static void initBacktrace()
{
    // This can catch and pretty-print all of the following:

    // SIGFPE
    // volatile int i = 2;
    // int zero = 0;
    // i /= zero;

    // SIGSEGV
    // *((int *)1) = 1;

    // uncaught arbitrary exception
    // throw 42;

    // uncaught std::exception derived exception (prints what())
    // throw std::logic_error("test output");

    printBacktrace = true;
    dumpCore = true;
    waitForGdbAttach = false;

    getOutputInformation(&useAnsiColor, nullptr, nullptr);

    demangleBufferSize = 512;
    demangleBuffer = (char *) malloc(demangleBufferSize);

    UnixSignalHandler::instance()->install(UnixSignalHandler::RawSignalHandler,
                                           { SIGFPE, SIGSEGV, SIGILL, SIGBUS, SIGPIPE, SIGABRT },
                                           [](int sig) {
        UnixSignalHandler::instance()->resetToDefault(sig);
        static char buffer[256];
        snprintf(buffer, sizeof(buffer), "uncaught signal %d (%s)", sig, UnixSignalHandler::signalName(sig));
        // 6 means to remove 6 stack frames: this way the backtrace starts at the point where
        // the signal reception interrupted the normal program flow
        crashHandler(buffer, 6);
    });

    std::set_terminate([]() {
        static char buffer [1024];

        auto type = abi::__cxa_current_exception_type();
        if (!type) {
            // 3 means to remove 3 stack frames: this way the backtrace starts at std::terminate
            crashHandler("terminate was called although no exception was thrown", 3);
        }

        const char *typeName = type->name();
        if (typeName) {
            int status;
            abi::__cxa_demangle(typeName, demangleBuffer, &demangleBufferSize, &status);
            if (status == 0 && *demangleBuffer) {
                typeName = demangleBuffer;
            }
        }
        try {
            throw;
        } catch (const std::exception &exc) {
            snprintf(buffer, sizeof(buffer), "uncaught exception of type %s (%s)", typeName, exc.what());
        } catch (...) {
            snprintf(buffer, sizeof(buffer), "uncaught exception of type %s", typeName);
        }

        // 4 means to remove 4 stack frames: this way the backtrace starts at std::terminate
        crashHandler(buffer, 4);
    });
}

static void crashHandler(const char *why, int stackFramesToIgnore)
{
    pid_t pid = getpid();
    char who[256];
    int whoLen = readlink("/proc/self/exe", who, sizeof(who) -1);
    who[qMax(0, whoLen)] = '\0';
    const char *title = ProcessTitle::title();

    fprintf(stderr, "\n*** process %s (%d) crashed ***\n\n > why: %s\n", title ? title : who, pid, why);

    if (printBacktrace) {
#if defined(AM_USE_LIBBACKTRACE) && defined(BACKTRACE_SUPPORTED)
        struct btData
        {
            backtrace_state *state;
            int level;
        };

        static auto printBacktraceLine = [](int level, const char *symbol, uintptr_t offset, const char *file = nullptr, int line = -1) {
            const char *fmt1 = " %3d: %s [%" PRIxPTR "]";
            const char *fmt2 = " in %s:%d";
            if (useAnsiColor) {
                fmt1 = " %3d: \x1b[1m%s\x1b[0m [\x1b[36m%" PRIxPTR "\x1b[0m]";
                fmt2 = " in \x1b[35m%s\x1b[0m:\x1b[35;1m%d\x1b[0m";
            }

            fprintf(stderr, fmt1, level, symbol, offset);
            if (file)
                fprintf(stderr, fmt2, file, line);
            fputs("\n", stderr);
        };

        static auto errorCallback = [](void *data, const char *msg, int errnum) {
            const char *fmt = " %3d: ERROR: %s (%d)\n";
            if (useAnsiColor)
                fmt = " %3d: \x1b[31;1mERROR: \x1b[0;1m%s (%d)\x1b[0m\n";

            fprintf(stderr, fmt, static_cast<btData *>(data)->level, msg, errnum);
        };

        static auto syminfoCallback = [](void *data, uintptr_t pc, const char *symname, uintptr_t symval, uintptr_t symsize) {
            Q_UNUSED(symval)
            Q_UNUSED(symsize)

            int level = static_cast<btData *>(data)->level;
            if (symname) {
                int status;
                abi::__cxa_demangle(symname, demangleBuffer, &demangleBufferSize, &status);

                if (status == 0 && *demangleBuffer)
                    printBacktraceLine(level, demangleBuffer, pc);
                else
                    printBacktraceLine(level, symname, pc);
            } else {
                printBacktraceLine(level, nullptr, pc);
            }
        };

        static auto fullCallback = [](void *data, uintptr_t pc, const char *filename, int lineno, const char *function) -> int {
            if (function) {
                int status;
                abi::__cxa_demangle(function, demangleBuffer, &demangleBufferSize, &status);

                printBacktraceLine(static_cast<btData *>(data)->level,
                                   (status == 0 && *demangleBuffer) ? demangleBuffer : function,
                                   pc, filename ? filename : "<unknown>", lineno);
            } else {
                backtrace_syminfo (static_cast<btData *>(data)->state, pc, syminfoCallback, errorCallback, data);
            }
            return 0;
        };

        static auto simpleCallback = [](void *data, uintptr_t pc) -> int {
            backtrace_pcinfo(static_cast<btData *>(data)->state, pc, fullCallback, errorCallback, data);
            static_cast<btData *>(data)->level++;
            return 0;
        };

        struct backtrace_state *state = backtrace_create_state(nullptr, BACKTRACE_SUPPORTS_THREADS,
                                                               errorCallback, nullptr);

        fprintf(stderr, "\n > backtrace:\n");
        btData data = { state, 0 };
        //backtrace_print(state, stackFramesToIgnore, stderr);
        backtrace_simple(state, stackFramesToIgnore, simpleCallback, errorCallback, &data);
#else // !defined(AM_USE_LIBBACKTRACE) && defined(BACKTRACE_SUPPORTED)
        Q_UNUSED(stackFramesToIgnore);
        void *addrArray[1024];
        int addrCount = backtrace(addrArray, sizeof(addrArray) / sizeof(*addrArray));

        if (!addrCount) {
            fprintf(stderr, " > no backtrace available\n");
        } else {
            char **symbols = backtrace_symbols(addrArray, addrCount);
            //backtrace_symbols_fd(addrArray, addrCount, 2);

            if (!symbols) {
                fprintf(stderr, " > no symbol names available\n");
            } else {
                fprintf(stderr, " > backtrace:\n");
                for (int i = 1; i < addrCount; ++i) {
                    char *function = nullptr;
                    char *offset = nullptr;
                    char *end = nullptr;

                    for (char *ptr = symbols[i]; ptr && *ptr; ++ptr) {
                        if (!function && *ptr == '(')
                            function = ptr + 1;
                        else if (function && !offset && *ptr == '+')
                            offset = ptr;
                        else if (function && !end && *ptr == ')')
                            end = ptr;
                    }

                    if (function && offset && end && (function != offset)) {
                        *offset = 0;
                        *end = 0;

                        int status;
                        abi::__cxa_demangle(function, demangleBuffer, &demangleBufferSize, &status);

                        if (status == 0 && *demangleBuffer) {
                            fprintf(stderr, " %3d: %s [+%s]\n", i, demangleBuffer, offset + 1);
                        } else {
                            fprintf(stderr, " %3d: %s [+%s]\n", i, function, offset + 1);
                        }
                    } else  {
                        fprintf(stderr, " %3d: %s\n", i, symbols[i]);
                    }
                }
                fprintf(stderr, "\n");
            }
        }
#endif // defined(AM_USE_LIBBACKTRACE) && defined(BACKTRACE_SUPPORTED)
    }
    if (waitForGdbAttach > 0) {
        fprintf(stderr, "\n > the process will be suspended for %d seconds and you can attach a debugger to it via\n\n   gdb -p %d\n",
                waitForGdbAttach, pid);
        static jmp_buf jmpenv;
        signal(SIGALRM, [](int) {
            longjmp(jmpenv, 1);
        });
        if (!setjmp(jmpenv)) {
            alarm(waitForGdbAttach);

            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGALRM);
            sigsuspend(&mask);
        } else {
            fprintf(stderr, "\n > no gdb attached\n");
        }
    }
    if (dumpCore) {
        fprintf(stderr, "\n > the process will be aborted (core dump)\n\n");
        UnixSignalHandler::instance()->resetToDefault({ SIGFPE, SIGSEGV, SIGILL, SIGBUS, SIGPIPE, SIGABRT });
        abort();
    }
    _exit(-1);
}

QT_END_NAMESPACE_AM

#endif // !Q_OS_LINUX

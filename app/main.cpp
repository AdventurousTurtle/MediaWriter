/*
 * Fedora Media Writer
 * Copyright (C) 2016 Martin Bříza <mbriza@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QLoggingCategory>
#include <QTranslator>
#include <QDebug>
#include <QScreen>
#include <QtPlugin>
#include <QElapsedTimer>
#include <QStandardPaths>

#ifdef __linux
#include <QX11Info>
#endif

#include "crashhandler.h"
#include "drivemanager.h"
#include "releasemanager.h"
#include "versionchecker.h"

#if QT_VERSION < 0x050300
# error "Minimum supported Qt version is 5.3.0"
#endif

#ifdef QT_STATIC
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);

Q_IMPORT_PLUGIN(QtQuick2Plugin);
Q_IMPORT_PLUGIN(QtQuick2WindowPlugin);
Q_IMPORT_PLUGIN(QtQuick2DialogsPlugin);
Q_IMPORT_PLUGIN(QtQuick2DialogsPrivatePlugin);
Q_IMPORT_PLUGIN(QtQuickControls1Plugin);
Q_IMPORT_PLUGIN(QtQuickLayoutsPlugin);
Q_IMPORT_PLUGIN(QmlFolderListModelPlugin);
Q_IMPORT_PLUGIN(QmlSettingsPlugin);
#endif

#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "main.h"

void foo() {
    int sockets[2];
    auto result = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
    PipeProcess p(nullptr, sockets[1]);
    p.setProgram("/usr/libexec/authopen");
    // /dev/disk2 is the same as /dev/rdisk2 but rdisk should use aligned writes
    p.setArguments({"-stdoutpipe", "-o", QString(O_RDWR), "/dev/disk2"});
    p.start(QIODevice::ReadOnly);

    int fd = -1;
    const size_t bufferSize = sizeof(struct cmsghdr) + sizeof(int);
    char buffer[bufferSize];

    struct iovec io_vec[1];
    io_vec[0].iov_len = bufferSize;
    io_vec[0].iov_base = buffer;

    const socklen_t socketSize = static_cast<socklen_t>(CMSG_SPACE(sizeof(int)));
    char cmsg_socket[socketSize];

    struct msghdr message = { 0 };
    message.msg_iov = io_vec;
    message.msg_iovlen = 1;
    message.msg_control = cmsg_socket;
    message.msg_controllen = socketSize;

    ssize_t size = recvmsg(sockets[0], &message, 0);
    if (size > 0) {
        std::cerr << "AYYY" << std::endl;
        struct cmsghdr *socketHeader = CMSG_FIRSTHDR(&message);
        if (socketHeader && socketHeader->cmsg_level == SOL_SOCKET && socketHeader->cmsg_type == SCM_RIGHTS) {
            fd = *reinterpret_cast<int*>(CMSG_DATA(socketHeader));

            QFile f;
            //f.open(fd, QIODevice::ReadWrite);
            //std::cerr<< f.read(10).data() << std::endl  ;
            //f.write("TEST DATA SHOULD APPEAR IN /dev/disk2");
            //f.close();
        }
    }
    else {
    }

    p.waitForFinished();

    QByteArray b(512, 'b');
    QFile f;
    f.open(fd, QIODevice::WriteOnly);
    f.write(b);
    fsync(fd);
    f.close();

    //p.write("!!!");
    //p.waitForBytesWritten();
    std::cerr << p.readAllStandardError().toStdString() << std::endl;
    std::cerr << p.readAllStandardOutput().toStdString() << std::endl;
    p.close();

}


int main(int argc, char **argv)
{
    //foo();
    //return 0;
    MessageHandler::install();
    CrashHandler::install();

#ifdef __linux
    if (QX11Info::isPlatformX11()) {
        if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP"))
            qputenv("QSG_RENDER_LOOP", "threaded");
        qputenv("GDK_BACKEND", "x11");
    }
#endif

    QApplication::setOrganizationDomain("fedoraproject.org");
    QApplication::setOrganizationName("fedoraproject.org");
    QApplication::setApplicationName("MediaWriter");

#ifdef __linux
    // qt x11 scaling is broken
    if (QX11Info::isPlatformX11())
#endif
        QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    options.parse(app.arguments());

    // considering how often we hit driver issues, I have decided to force
    // the QML software renderer on Windows and Linux, since Qt 5.9
#if QT_VERSION >= 0x050900
    if (qEnvironmentVariableIsEmpty("QMLSCENE_DEVICE"))
        qputenv("QMLSCENE_DEVICE", "softwarecontext");
#endif

    mDebug() << "Application constructed";

    QTranslator translator;
    translator.load(QLocale(QLocale().language(), QLocale().country()), QString(), QString(), ":/translations");
    app.installTranslator(&translator);

    mDebug() << "Injecting QML context properties";
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("drives", DriveManager::instance());
    engine.rootContext()->setContextProperty("releases", new ReleaseManager());
    engine.rootContext()->setContextProperty("downloadManager", DownloadManager::instance());
    engine.rootContext()->setContextProperty("mediawriterVersion", MEDIAWRITER_VERSION);
    engine.rootContext()->setContextProperty("versionChecker", new VersionChecker());
#if (defined(__linux) || defined(_WIN32))
    engine.rootContext()->setContextProperty("platformSupportsDelayedWriting", true);
#else
    engine.rootContext()->setContextProperty("platformSupportsDelayedWriting", false);
#endif
    mDebug() << "Loading the QML source code";
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    mDebug() << "Starting the application";
    int status = app.exec();
    mDebug() << "Quitting with status" << status;

    return status;
}

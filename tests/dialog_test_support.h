/***************************************************************************
 *   Copyright (C) 2026 by Bruno Alexandre Rosa                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef DIALOG_TEST_SUPPORT_H
#define DIALOG_TEST_SUPPORT_H

#include "interface/widgets/utility/device_files.h"
#include "support/test_home.h"

#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QList>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <gtest/gtest.h>
#include <memory>
#include <utility>

namespace DialogTest
{
inline void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// An empty path cancels.
inline void answerFileDialog(QFileDialog *files, const QString &path)
{
    if (path.isEmpty()) {
        files->reject();
    } else {
        auto *name = files->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        ASSERT_NE(name, nullptr) << "the file dialog has no name field: is it a native one?";
        // Typed, as selectFile() leaves a focused name field and its preselected file as they are.
        name->setText(path);
        // QFileDialog::accept() is protected; through QDialog it still validates the chosen file.
        static_cast<QDialog *>(files)->accept();
    }
    settle();
}

class DevicePicker
{
public:
    QString filter;
    QList<std::pair<QString, QByteArray>> saved;

    DevicePicker()
    {
        DeviceFiles::replaceForTesting(
            [this](const QString &nameFilter, const DeviceFiles::Opened &then) {
                filter = nameFilter;
                pending = then;
            },
            [this](const QByteArray &bytes, const QString &fileNameHint) { saved.append({fileNameHint, bytes}); });
    }
    ~DevicePicker()
    {
        DeviceFiles::replaceForTesting({}, {});
    }
    DevicePicker(const DevicePicker &) = delete;
    DevicePicker &operator=(const DevicePicker &) = delete;

    bool isOpen() const
    {
        return static_cast<bool>(pending);
    }

    // An empty name is how a browser reports a cancel.
    void pick(const QString &fileName, const QByteArray &bytes)
    {
        ASSERT_TRUE(isOpen()) << "no file was asked for";
        std::exchange(pending, {})(fileName, bytes);
        settle();
    }

private:
    DeviceFiles::Opened pending;
};

#ifdef Q_OS_MACOS
// ~QFileDialog saves to the QtProject preferences, which macOS writes in the real Library whatever the home is:
// a file dialog is kept once its owner is done with it, never destroyed.
class KeepFileDialogs : public QObject
{
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::DeferredDelete) {
            return false;
        }
        if (auto *files = qobject_cast<QFileDialog *>(watched)) {
            files->setParent(nullptr);
            return true;
        }
        return false;
    }
};
#endif

// A dialog that blocks runs a nested event loop: fail and close it instead of hanging.
class NestedLoopWatchdog
{
public:
    NestedLoopWatchdog()
    {
        QObject::connect(&timer, &QTimer::timeout, [] {
            if (QThread::currentThread()->loopLevel() == 0) {
                return;
            }
            ADD_FAILURE() << "a nested event loop is running";
            if (QWidget *popup = QApplication::activePopupWidget()) {
                popup->close();
            }
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible()) {
                    dialog->reject();
                }
            }
        });
        timer.start(10);
    }

private:
    QTimer timer;
};

class Fixture : public ::testing::Test // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    std::unique_ptr<QWidget> window = std::make_unique<QWidget>();

    template <typename Dialog> QList<Dialog *> openDialogs() const
    {
        QList<Dialog *> open;
        for (Dialog *dialog : window->findChildren<Dialog *>()) {
            if (dialog->isVisible()) {
                open.append(dialog);
            }
        }
        return open;
    }

    template <typename Dialog> Dialog *onlyOpen() const
    {
        const auto open = openDialogs<Dialog>();
        EXPECT_EQ(open.size(), 1);
        return open.value(0);
    }

    template <typename Dialog> static Dialog *onlyOpenTopLevel()
    {
        QList<Dialog *> open;
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<Dialog *>(widget); dialog && dialog->isVisible()) {
                open.append(dialog);
            }
        }
        EXPECT_EQ(open.size(), 1);
        return open.value(0);
    }

private:
    NestedLoopWatchdog nestedLoopWatchdog;
};

template <typename RunTests> int run(int argc, char **argv, const QString &applicationName, RunTests runTests)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // A CI container's user has no home it can write to: saved settings and file dialogs both use it.
    const test_support::TestHome home;
    // A platform theme's native file dialog has none of the widgets the tests answer through.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
#ifdef Q_OS_MACOS
    KeepFileDialogs keepFileDialogs;
    app.installEventFilter(&keepFileDialogs);
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("CockatriceTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("%1-%2").arg(applicationName).arg(QCoreApplication::applicationPid()));
    ::testing::InitGoogleTest(&argc, argv);
    // CMake lists the cases at build time, where LeakSanitizer is on: the settings singleton leaks at exit.
    if (::testing::GTEST_FLAG(list_tests)) {
        return RUN_ALL_TESTS();
    }
    // In portable mode the settings live beside the binary: a run would write there and then delete them.
    if (QFile::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/portable.dat"))) {
        qCritical() << "portable.dat is beside the test binary: refusing to run on portable data";
        return 1;
    }
    if (!home.writesStayInside()) {
        return 1;
    }
    return runTests();
}

inline int run(int argc, char **argv, const QString &applicationName)
{
    return run(argc, argv, applicationName, [] { return RUN_ALL_TESTS(); });
}
} // namespace DialogTest

#endif

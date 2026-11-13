#ifndef DIALOG_TEST_SUPPORT_H
#define DIALOG_TEST_SUPPORT_H

#include <QApplication>
#include <QDialog>
#include <QList>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <gtest/gtest.h>
#include <memory>

namespace DialogTest
{
inline void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

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

private:
    NestedLoopWatchdog nestedLoopWatchdog;
};

inline int run(
    int argc,
    char **argv,
    const QString &applicationName,
    const std::function<int()> &runTests = [] { return RUN_ALL_TESTS(); })
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // A CI container's user has no home it can write to: saved settings and file dialogs both use it.
    const QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CockatriceTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("%1-%2").arg(applicationName).arg(QCoreApplication::applicationPid()));
    ::testing::InitGoogleTest(&argc, argv);
    // CMake lists the cases at build time, where LeakSanitizer is on: the settings singleton leaks at exit.
    if (::testing::GTEST_FLAG(list_tests)) {
        return RUN_ALL_TESTS();
    }
    return runTests();
}
} // namespace DialogTest

#endif

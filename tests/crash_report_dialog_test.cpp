#include "client/crash/crash_reporter.h"
#include "interface/widgets/dialogs/dlg_crash_report.h"
#include "interface/widgets/utility/non_blocking_dialogs.h"

#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <gtest/gtest.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <memory>
#include <optional>

RNG_Abstract *rng = nullptr; // NOSONAR: the client declares it extern and assigns it

namespace
{
void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void settleBoxThenDialog()
{
    settle();
    settle();
}

// Built without crash reporting, the reporter fails every send.
class CrashReportDialog : public ::testing::Test // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    std::unique_ptr<QWidget> window = std::make_unique<QWidget>();
    CrashReporter reporter;
    DlgCrashReport *dialog = nullptr;
    std::optional<int> result;
    QTimer nestedLoopWatchdog;

    void SetUp() override
    {
        // A dialog that blocks runs a nested event loop: fail and close it instead of hanging.
        QObject::connect(&nestedLoopWatchdog, &QTimer::timeout, [] {
            if (QThread::currentThread()->loopLevel() == 0) {
                return;
            }
            ADD_FAILURE() << "a nested event loop is running";
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *open = qobject_cast<QDialog *>(widget); open && open->isVisible()) {
                    open->reject();
                }
            }
        });
        nestedLoopWatchdog.start(10);

        dialog = new DlgCrashReport(window.get(), true, &reporter);
        Dialogs::open(dialog, [this](int finished) { result = finished; });
    }

    QMessageBox *openBox() const
    {
        const auto boxes = dialog->findChildren<QMessageBox *>();
        EXPECT_EQ(boxes.size(), 1);
        return boxes.value(0);
    }

    QMessageBox *sendAndFail() const
    {
        for (QPushButton *button : dialog->findChildren<QPushButton *>()) {
            if (button->text() == "Send") {
                button->click();
                break;
            }
        }
        return openBox();
    }
};

TEST_F(CrashReportDialog, AFailedSendAsksWhatToDoWithTheReport)
{
    QMessageBox *box = sendAndFail();

    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->isVisible());
    EXPECT_EQ(box->icon(), QMessageBox::Warning);
    EXPECT_EQ(box->standardButtons(), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    EXPECT_EQ(box->defaultButton(), box->button(QMessageBox::Save));
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_FALSE(result.has_value());
}

TEST_F(CrashReportDialog, DiscardingAFailedReportRejectsTheDialog)
{
    QMessageBox *box = sendAndFail();
    ASSERT_NE(box, nullptr);

    box->button(QMessageBox::Discard)->click();
    settleBoxThenDialog();

    EXPECT_EQ(result, int(QDialog::Rejected));
}

TEST_F(CrashReportDialog, CancellingAFailedReportKeepsTheDialogOpen)
{
    QMessageBox *box = sendAndFail();
    ASSERT_NE(box, nullptr);

    box->close();
    settle();

    EXPECT_TRUE(dialog->isVisible());
    EXPECT_FALSE(result.has_value());
}

TEST_F(CrashReportDialog, SavingAFailedReportClosesTheDialogAfterTheSavedNotice)
{
    QMessageBox *box = sendAndFail();
    ASSERT_NE(box, nullptr);

    box->button(QMessageBox::Save)->click();
    settle();

    QMessageBox *notice = openBox();
    ASSERT_NE(notice, nullptr);
    EXPECT_EQ(notice->icon(), QMessageBox::Information);
    const QString savedPath = notice->text().section('\n', 1);
    EXPECT_TRUE(QFile::remove(savedPath)) << savedPath.toStdString();
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_FALSE(result.has_value());

    notice->button(QMessageBox::Ok)->click();
    settleBoxThenDialog();

    EXPECT_EQ(result, int(QDialog::Accepted));
}
} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CockatriceTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("crash-report-dialog-test-%1").arg(QCoreApplication::applicationPid()));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

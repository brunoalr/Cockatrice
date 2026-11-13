#include "client/settings/cache_settings.h"
#include "game_graphics/board/abstract_counter.h"
#include "game_graphics/dialogs/dlg_move_top_cards_until.h"
#include "game_test_fixture.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QGraphicsRectItem>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <gtest/gtest.h>
#include <libcockatrice/game/board/counter_state.h>
#include <libcockatrice/protocol/pb/command_set_counter.pb.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <memory>

RNG_Abstract *rng = nullptr; // NOSONAR: the client declares it extern and assigns it

namespace
{
void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

class TestCounter : public AbstractCounter
{
public:
    using AbstractCounter::AbstractCounter;

    QRectF boundingRect() const override
    {
        return {0, 0, 10, 10};
    }

    void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override
    {
    }
};

class GameDialogs : public ::testing::Test // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    std::unique_ptr<QWidget> window = std::make_unique<QWidget>();
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
                if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->isVisible()) {
                    dialog->reject();
                }
            }
        });
        nestedLoopWatchdog.start(10);
    }

    template <typename Dialog> static QList<Dialog *> openDialogs()
    {
        QList<Dialog *> open;
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<Dialog *>(widget); dialog && dialog->isVisible()) {
                open.append(dialog);
            }
        }
        return open;
    }

    template <typename Dialog> static Dialog *onlyOpen()
    {
        const auto open = openDialogs<Dialog>();
        EXPECT_EQ(open.size(), 1);
        return open.value(0);
    }

    static void clickInOnlyBox(QMessageBox::StandardButton button)
    {
        const QMessageBox *box = onlyOpen<QMessageBox>();
        ASSERT_NE(box, nullptr);
        box->button(button)->click();
        settle();
    }

    DlgMoveTopCardsUntil *openMoveUntil(const QString &expr) const
    {
        auto *dialog = new DlgMoveTopCardsUntil(window.get(), {.exprs = {"older"}, .numberOfHits = 1});
        dialog->findChild<QComboBox *>()->setCurrentText(expr);
        dialog->show();
        return dialog;
    }

    static void clickOk(const DlgMoveTopCardsUntil *dialog)
    {
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        settle();
    }
};

TEST_F(GameDialogs, AnInvalidSearchExpressionWarnsAndKeepsTheDialogOpen)
{
    const DlgMoveTopCardsUntil *dialog = openMoveUntil("t:(");

    clickOk(dialog);
    clickInOnlyBox(QMessageBox::Ok);

    EXPECT_TRUE(dialog->isVisible());
}

TEST_F(GameDialogs, AnExpressionWithoutAMatchIsUsedOnlyAfterYes)
{
    const DlgMoveTopCardsUntil *dialog = openMoveUntil("no such card");

    clickOk(dialog);
    clickInOnlyBox(QMessageBox::No);
    EXPECT_TRUE(dialog->isVisible());

    clickOk(dialog);
    clickInOnlyBox(QMessageBox::Yes);

    EXPECT_FALSE(dialog->isVisible());
    EXPECT_EQ(dialog->result(), QDialog::Accepted);
    EXPECT_EQ(dialog->getExpr(), "no such card");
}

class CounterDialog : public GameDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    GameHarness harness;
    std::unique_ptr<QGraphicsRectItem> board = std::make_unique<QGraphicsRectItem>();
    QPointer<TestCounter> counter;

    void SetUp() override
    {
        GameDialogs::SetUp();
        CounterState *state = harness.player()->addCounter(3, "life", Qt::white, 20, 20);
        counter = new TestCounter(state, harness.player(), true, false, board.get());
    }

    AbstractCounterDialog *openSetCounter() const
    {
        counter->getMenu()->actions().first()->trigger();
        settle();
        return onlyOpen<AbstractCounterDialog>();
    }
};

TEST_F(CounterDialog, SettingTheCounterSendsTheNewValue)
{
    AbstractCounterDialog *dialog = openSetCounter();
    ASSERT_NE(dialog, nullptr);
    harness.client()->sent.clear();

    dialog->setTextValue("7");
    dialog->accept();
    settle();

    ASSERT_EQ(harness.client()->sent.size(), 1);
    const auto &command = harness.lastCommand().game_command(0).GetExtension(Command_SetCounter::ext);
    EXPECT_EQ(command.counter_id(), 3);
    EXPECT_EQ(command.value(), 7);
}

TEST_F(CounterDialog, ACounterRemovedWhileItsDialogIsOpenGoesWhenTheDialogCloses)
{
    AbstractCounterDialog *dialog = openSetCounter();
    ASSERT_NE(dialog, nullptr);

    counter->delCounter();
    settle();
    EXPECT_FALSE(counter.isNull());

    dialog->reject();
    settle();

    EXPECT_TRUE(counter.isNull());
}
} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // A CI container's user has no home it can write to, and the settings are saved there.
    const QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CockatriceTest"));
    QCoreApplication::setApplicationName(
        QStringLiteral("game-dialogs-test-%1").arg(QCoreApplication::applicationPid()));
    ::testing::InitGoogleTest(&argc, argv);
    // CMake lists the cases at build time, where LeakSanitizer is on: the settings singleton leaks at exit.
    if (::testing::GTEST_FLAG(list_tests)) {
        return RUN_ALL_TESTS();
    }

    const int result = RUN_ALL_TESTS();

    QDir(SettingsCache::instance().getDataPath()).removeRecursively();
    QDir(SettingsCache::instance().getCachePath()).removeRecursively();
    return result;
}

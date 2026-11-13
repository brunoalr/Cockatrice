#include "interface/widgets/utility/non_blocking_dialogs.h"

#include <QApplication>
#include <QColorDialog>
#include <QInputDialog>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{
void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// Qt delivers a deferred delete posted inside an event handler on the next pass, ahead of calls queued
// after it, as in the running client. One posted from the test body waits for settle().
void fromTheEventLoop(std::function<void()> action)
{
    QMetaObject::invokeMethod(qApp, std::move(action), Qt::QueuedConnection);
    QCoreApplication::processEvents();
    settle();
}

class NonBlockingDialogs : public ::testing::Test
{
protected:
    std::unique_ptr<QWidget> parent = std::make_unique<QWidget>();
    QTimer nestedLoopWatchdog;

    void SetUp() override
    {
        // A helper that blocks runs a nested event loop: fail and close its dialog instead of hanging.
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

    QMessageBox *onlyMessageBox() const
    {
        const auto boxes = parent->findChildren<QMessageBox *>();
        EXPECT_EQ(boxes.size(), 1);
        return boxes.value(0);
    }

    QInputDialog *onlyInputDialog() const
    {
        const auto dialogs = parent->findChildren<QInputDialog *>();
        EXPECT_EQ(dialogs.size(), 1);
        return dialogs.value(0);
    }
};

TEST_F(NonBlockingDialogs, ConfirmReturnsBeforeTheCallbackRuns)
{
    std::optional<bool> answer;

    Dialogs::confirm(parent.get(), "Title", "Text", [&](bool confirmed) { answer = confirmed; });

    EXPECT_FALSE(answer.has_value());
    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->isVisible());
    EXPECT_EQ(box->windowModality(), Qt::WindowModal);
}

TEST_F(NonBlockingDialogs, ConfirmShowsAQuestionWithYesAndNo)
{
    Dialogs::confirm(parent.get(), "Title", "Text", [](bool) {}, QMessageBox::No);

    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->icon(), QMessageBox::Question);
    // QMessageBox ignores its window title on macOS.
#ifndef Q_OS_MACOS
    EXPECT_EQ(box->windowTitle(), "Title");
#endif
    EXPECT_EQ(box->text(), "Text");
    EXPECT_EQ(box->standardButtons(), QMessageBox::Yes | QMessageBox::No);
    EXPECT_EQ(box->defaultButton(), box->button(QMessageBox::No));
}

TEST_F(NonBlockingDialogs, ConfirmYesGivesTrue)
{
    std::optional<bool> answer;
    Dialogs::confirm(parent.get(), "Title", "Text", [&](bool confirmed) { answer = confirmed; });

    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    box->button(QMessageBox::Yes)->click();
    settle();

    EXPECT_EQ(answer, true);
}

TEST_F(NonBlockingDialogs, ConfirmNoGivesFalse)
{
    std::optional<bool> answer;
    Dialogs::confirm(parent.get(), "Title", "Text", [&](bool confirmed) { answer = confirmed; });

    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    box->button(QMessageBox::No)->click();
    settle();

    EXPECT_EQ(answer, false);
}

TEST_F(NonBlockingDialogs, RejectingTheConfirmBoxGivesFalse)
{
    std::optional<bool> answer;
    Dialogs::confirm(parent.get(), "Title", "Text", [&](bool confirmed) { answer = confirmed; });

    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    box->reject();
    settle();

    EXPECT_EQ(answer, false);
}

TEST_F(NonBlockingDialogs, ClosingTheConfirmBoxGivesFalse)
{
    std::optional<bool> answer;
    Dialogs::confirm(parent.get(), "Title", "Text", [&](bool confirmed) { answer = confirmed; });

    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    box->close();
    settle();

    EXPECT_EQ(answer, false);
}

TEST_F(NonBlockingDialogs, AskReportsTheClickedButton)
{
    std::optional<QMessageBox::StandardButton> answer;
    Dialogs::ask(parent.get(), QMessageBox::Warning, "Title", "Text",
                 QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save,
                 [&](QMessageBox::StandardButton clicked) { answer = clicked; });

    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->icon(), QMessageBox::Warning);
    box->button(QMessageBox::Discard)->click();
    settle();

    EXPECT_EQ(answer, QMessageBox::Discard);
}

TEST_F(NonBlockingDialogs, ClosingAskGivesTheEscapeButton)
{
    std::optional<QMessageBox::StandardButton> answer;
    Dialogs::ask(parent.get(), QMessageBox::Warning, "Title", "Text",
                 QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save,
                 [&](QMessageBox::StandardButton clicked) { answer = clicked; });

    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    box->close();
    settle();

    EXPECT_EQ(answer, QMessageBox::Cancel);
}

TEST_F(NonBlockingDialogs, RejectingAskWithoutAnEscapeButtonGivesNoButton)
{
    std::optional<QMessageBox::StandardButton> answer;
    Dialogs::ask(parent.get(), QMessageBox::Question, "Title", "Text", QMessageBox::Save | QMessageBox::Discard,
                 QMessageBox::Save, [&](QMessageBox::StandardButton clicked) { answer = clicked; });

    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    box->reject();
    settle();

    EXPECT_EQ(answer, QMessageBox::NoButton);
}

TEST_F(NonBlockingDialogs, AskWithoutADefaultPicksTheAcceptButtonLikeTheStaticCalls)
{
    // Yes comes first but has YesRole: the static calls skip it for Retry, the first AcceptRole button.
    Dialogs::ask(parent.get(), QMessageBox::Question, "Title", "Text", QMessageBox::Yes | QMessageBox::Retry,
                 QMessageBox::NoButton, [](QMessageBox::StandardButton) {});

    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->defaultButton(), box->button(QMessageBox::Retry));
}

TEST_F(NonBlockingDialogs, ConfirmWithoutADefaultSetsNoneLikeTheStaticCall)
{
    Dialogs::confirm(parent.get(), "Title", "Text", [](bool) {});

    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->defaultButton(), nullptr);
}

TEST_F(NonBlockingDialogs, NoticesOpenOneBoxThatAcceptingDeletes)
{
    using Notice = void (*)(QWidget *, const QString &, const QString &, std::function<void()>);
    const std::vector<std::pair<Notice, QMessageBox::Icon>> notices{
        {&Dialogs::information, QMessageBox::Information},
        {&Dialogs::warning, QMessageBox::Warning},
        {&Dialogs::critical, QMessageBox::Critical},
    };
    for (const auto &[notice, icon] : notices) {
        notice(parent.get(), "Title", "Text", {});

        QPointer<QMessageBox> box = onlyMessageBox();
        ASSERT_NE(box, nullptr);
        EXPECT_EQ(box->icon(), icon);
#ifndef Q_OS_MACOS
        EXPECT_EQ(box->windowTitle(), "Title");
#endif
        EXPECT_EQ(box->text(), "Text");
        EXPECT_TRUE(box->isVisible());

        box->accept();
        settle();

        EXPECT_TRUE(box.isNull());
    }
}

TEST_F(NonBlockingDialogs, DestroyingTheParentWhileConfirmIsOpenRunsNoCallback)
{
    bool called = false;
    Dialogs::confirm(parent.get(), "Title", "Text", [&](bool) { called = true; });
    QPointer<QMessageBox> box = onlyMessageBox();
    ASSERT_NE(box, nullptr);

    parent.reset();
    settle();

    EXPECT_TRUE(box.isNull());
    EXPECT_FALSE(called);
}

TEST_F(NonBlockingDialogs, GetTextDeliversTheTypedTextOnAccept)
{
    std::optional<QString> text;
    Dialogs::getText(parent.get(), "Title", "Label", "initial", [&](const QString &typed) { text = typed; });

    QInputDialog *dialog = onlyInputDialog();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->windowTitle(), "Title");
    EXPECT_EQ(dialog->labelText(), "Label");
    EXPECT_EQ(dialog->textValue(), "initial");
    EXPECT_FALSE(text.has_value());
    dialog->setTextValue("typed");
    dialog->accept();
    settle();

    EXPECT_EQ(text, "typed");
}

TEST_F(NonBlockingDialogs, GetTextDeliversNothingOnReject)
{
    bool called = false;
    Dialogs::getText(parent.get(), "Title", "Label", "initial", [&](const QString &) { called = true; });

    QPointer<QInputDialog> dialog = onlyInputDialog();
    ASSERT_NE(dialog, nullptr);
    dialog->reject();
    settle();

    EXPECT_FALSE(called);
    EXPECT_TRUE(dialog.isNull());
}

TEST_F(NonBlockingDialogs, GetMultiLineTextUsesAPlainTextEdit)
{
    std::optional<QString> text;
    Dialogs::getMultiLineText(parent.get(), "Title", "Label", "a\nb", [&](const QString &typed) { text = typed; });

    QInputDialog *dialog = onlyInputDialog();
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(dialog->testOption(QInputDialog::UsePlainTextEditForTextInput));
    dialog->accept();
    settle();

    EXPECT_EQ(text, "a\nb");
}

TEST_F(NonBlockingDialogs, GetIntDeliversTheValueOnAccept)
{
    std::optional<int> value;
    Dialogs::getInt(parent.get(), "Title", "Label", 3, 1, 10, 2, [&](int entered) { value = entered; });

    QInputDialog *dialog = onlyInputDialog();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->intValue(), 3);
    EXPECT_EQ(dialog->intMinimum(), 1);
    EXPECT_EQ(dialog->intMaximum(), 10);
    EXPECT_EQ(dialog->intStep(), 2);
    dialog->setIntValue(7);
    dialog->accept();
    settle();

    EXPECT_EQ(value, 7);
}

TEST_F(NonBlockingDialogs, GetItemDeliversTheItemOnAccept)
{
    std::optional<QString> item;
    Dialogs::getItem(parent.get(), "Title", "Label", {"a", "b", "c"}, 1, false,
                     [&](const QString &chosen) { item = chosen; });

    QInputDialog *dialog = onlyInputDialog();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->comboBoxItems(), QStringList({"a", "b", "c"}));
    EXPECT_EQ(dialog->textValue(), "b");
    EXPECT_FALSE(dialog->isComboBoxEditable());
    dialog->setTextValue("c");
    dialog->accept();
    settle();

    EXPECT_EQ(item, "c");
}

TEST_F(NonBlockingDialogs, GetColorDeliversTheChosenColorOnAcceptOnly)
{
    std::optional<QColor> color;
    Dialogs::getColor(Qt::red, parent.get(), "Title", [&](const QColor &chosen) { color = chosen; });

    QColorDialog *dialog = parent->findChild<QColorDialog *>();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->currentColor(), QColor(Qt::red));
    EXPECT_EQ(dialog->windowTitle(), "Title");
    dialog->reject();
    settle();
    EXPECT_FALSE(color.has_value());
    ASSERT_TRUE(parent->findChildren<QColorDialog *>().isEmpty());

    Dialogs::getColor(Qt::red, parent.get(), "Title", [&](const QColor &chosen) { color = chosen; });
    ASSERT_EQ(parent->findChildren<QColorDialog *>().size(), 1);
    dialog = parent->findChild<QColorDialog *>();
    dialog->setCurrentColor(Qt::blue);
    dialog->accept();
    settle();

    EXPECT_EQ(color, QColor(Qt::blue));
}

TEST_F(NonBlockingDialogs, OpenReportsTheResultWhileTheDialogIsStillReadable)
{
    for (const int expected : {int(QDialog::Accepted), int(QDialog::Rejected)}) {
        std::optional<int> result;
        QString read;
        auto *dialog = new QInputDialog(parent.get());
        dialog->setTextValue("field");
        QPointer<QInputDialog> guard = dialog;

        Dialogs::open(dialog, [&, dialog](int finished) {
            result = finished;
            read = dialog->textValue();
        });
        EXPECT_TRUE(dialog->isVisible());
        EXPECT_FALSE(result.has_value());

        dialog->done(expected);
        settle();

        EXPECT_EQ(result, expected);
        EXPECT_EQ(read, "field");
        EXPECT_TRUE(guard.isNull());
    }
}

TEST_F(NonBlockingDialogs, EmptyCallbacksAreAllowed)
{
    Dialogs::confirm(parent.get(), "Title", "Text", {});
    QMessageBox *box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    box->button(QMessageBox::Yes)->click();
    settle();

    Dialogs::ask(parent.get(), QMessageBox::Question, "Title", "Text", QMessageBox::Ok, QMessageBox::Ok, {});
    box = onlyMessageBox();
    ASSERT_NE(box, nullptr);
    box->button(QMessageBox::Ok)->click();
    settle();

    auto *customBox = new QMessageBox(parent.get());
    Dialogs::open(customBox, std::function<void(QAbstractButton *)>{});
    customBox->reject();
    settle();

    Dialogs::getText(parent.get(), "Title", "Label", "text", {});
    QInputDialog *input = onlyInputDialog();
    ASSERT_NE(input, nullptr);
    input->accept();
    settle();

    auto *plain = new QDialog(parent.get());
    QPointer<QDialog> dialog = plain;
    Dialogs::open(plain, {});
    plain->accept();
    settle();

    EXPECT_TRUE(dialog.isNull());
    EXPECT_TRUE(parent->findChildren<QDialog *>().isEmpty());
}

TEST_F(NonBlockingDialogs, TheCallbackRunsAfterTheDialogsCloseHandling)
{
    int calls = 0;
    Dialogs::confirm(parent.get(), "Title", "Text", [&](bool) { ++calls; });

    onlyMessageBox()->button(QMessageBox::Yes)->click();

    EXPECT_EQ(calls, 0) << "the callback ran inside the click that closed the box";
    settle();
    EXPECT_EQ(calls, 1);
}

TEST_F(NonBlockingDialogs, ACallbackMayDestroyTheParent)
{
    QWidget *owner = parent.release();
    bool ran = false;
    Dialogs::confirm(owner, "Title", "Text", [&](bool) {
        ran = true;
        delete owner;
    });

    owner->findChild<QMessageBox *>()->button(QMessageBox::Yes)->click();
    settle();

    EXPECT_TRUE(ran);
}

TEST_F(NonBlockingDialogs, ACallbackMayOpenAnotherDialog)
{
    Dialogs::confirm(parent.get(), "First", "Text",
                     [&](bool) { Dialogs::information(parent.get(), "Second", "Text"); });

    onlyMessageBox()->button(QMessageBox::Yes)->click();
    settle();

    QMessageBox *second = onlyMessageBox();
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->icon(), QMessageBox::Information);
    EXPECT_TRUE(second->isVisible());
}

TEST_F(NonBlockingDialogs, OpenBoxReportsTheClickedCustomButton)
{
    auto *box = new QMessageBox(parent.get());
    box->addButton("Keep", QMessageBox::YesRole);
    QPushButton *drop = box->addButton("Drop", QMessageBox::DestructiveRole);
    box->addButton("Back", QMessageBox::RejectRole);
    QAbstractButton *clicked = nullptr;
    int calls = 0;
    Dialogs::open(box, [&](QAbstractButton *button) {
        clicked = button;
        ++calls;
    });

    drop->click();
    settle();

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(clicked, drop);
}

TEST_F(NonBlockingDialogs, ClosingAnOpenBoxReportsItsOnlyRejectButton)
{
    auto *box = new QMessageBox(parent.get());
    box->addButton("Keep", QMessageBox::YesRole);
    box->addButton("Drop", QMessageBox::DestructiveRole);
    QPushButton *back = box->addButton("Back", QMessageBox::RejectRole);
    QAbstractButton *clicked = nullptr;
    Dialogs::open(box, [&](QAbstractButton *button) { clicked = button; });

    box->close();
    settle();

    EXPECT_EQ(clicked, back);
}

TEST_F(NonBlockingDialogs, ClosingAnOpenBoxReportsTheEscapeButtonSetOnIt)
{
    auto *box = new QMessageBox(parent.get());
    QPushButton *keep = box->addButton("Keep", QMessageBox::YesRole);
    box->addButton("Back", QMessageBox::RejectRole);
    box->setEscapeButton(keep);
    QAbstractButton *clicked = nullptr;
    Dialogs::open(box, [&](QAbstractButton *button) { clicked = button; });

    box->close();
    settle();

    EXPECT_EQ(clicked, keep);
}

TEST_F(NonBlockingDialogs, RejectingAnOpenBoxWithoutAnEscapeButtonReportsNull)
{
    auto *box = new QMessageBox(parent.get());
    QPushButton *keep = box->addButton("Keep", QMessageBox::YesRole);
    box->addButton("Drop", QMessageBox::YesRole);
    QAbstractButton *clicked = keep;
    int calls = 0;
    Dialogs::open(box, [&](QAbstractButton *button) {
        clicked = button;
        ++calls;
    });

    box->reject();
    settle();

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(clicked, nullptr);
}

TEST_F(NonBlockingDialogs, ClosingAskWithOneButtonGivesThatButton)
{
    std::optional<QMessageBox::StandardButton> answer;
    Dialogs::ask(parent.get(), QMessageBox::Information, "Title", "Text", QMessageBox::Ok, QMessageBox::Ok,
                 [&](QMessageBox::StandardButton button) { answer = button; });

    onlyMessageBox()->close();
    settle();

    EXPECT_EQ(answer, QMessageBox::Ok);
}

TEST_F(NonBlockingDialogs, ClosingAskPrefersCancelOverAnotherRejectRoleButton)
{
    std::optional<QMessageBox::StandardButton> answer;
    Dialogs::ask(parent.get(), QMessageBox::Question, "Title", "Text",
                 QMessageBox::Ok | QMessageBox::Cancel | QMessageBox::Close, QMessageBox::Ok,
                 [&](QMessageBox::StandardButton button) { answer = button; });

    onlyMessageBox()->close();
    settle();

    EXPECT_EQ(answer, QMessageBox::Cancel);
}

TEST_F(NonBlockingDialogs, ClosingAskGivesItsOnlyRejectRoleButton)
{
    std::optional<QMessageBox::StandardButton> answer;
    Dialogs::ask(parent.get(), QMessageBox::Question, "Title", "Text", QMessageBox::Yes | QMessageBox::Close,
                 QMessageBox::Yes, [&](QMessageBox::StandardButton button) { answer = button; });

    onlyMessageBox()->close();
    settle();

    EXPECT_EQ(answer, QMessageBox::Close);
}

TEST_F(NonBlockingDialogs, ClosingAskGivesItsOnlyNoRoleButton)
{
    std::optional<QMessageBox::StandardButton> answer;
    Dialogs::ask(parent.get(), QMessageBox::Question, "Title", "Text", QMessageBox::Yes | QMessageBox::No,
                 QMessageBox::Yes, [&](QMessageBox::StandardButton button) { answer = button; });

    onlyMessageBox()->close();
    settle();

    EXPECT_EQ(answer, QMessageBox::No);
}

TEST_F(NonBlockingDialogs, RejectingAskWithTwoNoRoleButtonsGivesNoButton)
{
    std::optional<QMessageBox::StandardButton> answer;
    Dialogs::ask(parent.get(), QMessageBox::Question, "Title", "Text",
                 QMessageBox::Yes | QMessageBox::No | QMessageBox::NoToAll, QMessageBox::Yes,
                 [&](QMessageBox::StandardButton button) { answer = button; });

    onlyMessageBox()->reject();
    settle();

    EXPECT_EQ(answer, QMessageBox::NoButton);
}

TEST_F(NonBlockingDialogs, ANoticeRunsItsCallbackOnceAfterOk)
{
    int calls = 0;
    Dialogs::warning(parent.get(), "Title", "Text", [&] { ++calls; });

    EXPECT_EQ(calls, 0);
    onlyMessageBox()->button(QMessageBox::Ok)->click();
    settle();

    EXPECT_EQ(calls, 1);
}

TEST_F(NonBlockingDialogs, ANoticeRunsItsCallbackWhenClosedFromItsWindow)
{
    int calls = 0;
    Dialogs::critical(parent.get(), "Title", "Text", [&] { ++calls; });

    onlyMessageBox()->close();
    settle();

    EXPECT_EQ(calls, 1);
}

TEST_F(NonBlockingDialogs, DestroyingTheParentWhileANoticeIsOpenRunsNoCallback)
{
    int calls = 0;
    Dialogs::information(parent.get(), "Title", "Text", [&] { ++calls; });

    parent.reset();
    settle();

    EXPECT_EQ(calls, 0);
}

TEST_F(NonBlockingDialogs, GetTextCanHideWhatIsTyped)
{
    Dialogs::getText(parent.get(), "Title", "Label", "", [](const QString &) {}, QLineEdit::Password);

    QInputDialog *dialog = onlyInputDialog();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->textEchoMode(), QLineEdit::Password);
}

TEST_F(NonBlockingDialogs, GetColorCanKeepTheAlphaChannel)
{
    std::optional<QColor> color;
    Dialogs::getColor(
        Qt::red, parent.get(), "Title", [&](const QColor &chosen) { color = chosen; }, QColorDialog::ShowAlphaChannel);

    QColorDialog *dialog = parent->findChild<QColorDialog *>();
    ASSERT_NE(dialog, nullptr);
    dialog->setCurrentColor(QColor(10, 20, 30, 40));
    dialog->accept();
    settle();

    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(color->alpha(), 40);
}

TEST_F(NonBlockingDialogs, ADialogWithoutAParentBlocksTheWholeApplication)
{
    std::optional<QString> text;
    Dialogs::getText(nullptr, "Title", "Label", "typed", [&](const QString &entered) { text = entered; });

    QPointer<QInputDialog> dialog;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *candidate = qobject_cast<QInputDialog *>(widget)) {
            dialog = candidate;
        }
    }
    ASSERT_FALSE(dialog.isNull());
    EXPECT_EQ(dialog->windowModality(), Qt::ApplicationModal);

    dialog->accept();
    settle();

    EXPECT_EQ(text, "typed");
    EXPECT_TRUE(dialog.isNull());
}

TEST_F(NonBlockingDialogs, ADialogThatDeletesItselfOnCloseOutlivesItsCallback)
{
    auto *dialog = new QInputDialog(parent.get());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<QInputDialog> guard = dialog;
    std::optional<bool> aliveInCallback;
    Dialogs::open(dialog, [&](int) { aliveInCallback = !guard.isNull(); });

    fromTheEventLoop([dialog] { dialog->accept(); });

    EXPECT_EQ(aliveInCallback, true);
    EXPECT_TRUE(guard.isNull());
}

TEST_F(NonBlockingDialogs, ADialogThatDeletesItselfOnCloseStillAnswersWithoutAParent)
{
    auto *dialog = new QInputDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<QInputDialog> guard = dialog;
    std::optional<int> result;
    Dialogs::open(dialog, [&](int finished) { result = finished; });

    fromTheEventLoop([dialog] { dialog->accept(); });

    EXPECT_EQ(result, int(QDialog::Accepted));
    EXPECT_TRUE(guard.isNull());
}

TEST_F(NonBlockingDialogs, ClosingAnOpenBoxWithDetailsReportsItsOtherButton)
{
    auto *box = new QMessageBox(QMessageBox::Information, "Title", "Text", QMessageBox::Ok, parent.get());
    box->setDetailedText("Details");
    QAbstractButton *ok = box->button(QMessageBox::Ok);
    ASSERT_NE(ok, nullptr);
    QAbstractButton *clicked = nullptr;
    Dialogs::open(box, [&](QAbstractButton *button) { clicked = button; });

    box->close();
    settle();

    EXPECT_EQ(clicked, ok);
}

TEST_F(NonBlockingDialogs, ADialogThatFinishesTwiceAnswersOnce)
{
    auto *dialog = new QInputDialog(parent.get());
    int calls = 0;
    Dialogs::open(dialog, [&](int) { ++calls; });

    dialog->accept();
    dialog->accept();
    settle();

    EXPECT_EQ(calls, 1);
}

TEST_F(NonBlockingDialogs, ACallbackMayOpenItsDialogAgain)
{
    auto *dialog = new QInputDialog(parent.get());
    QPointer<QInputDialog> guard = dialog;
    int secondAnswers = 0;
    Dialogs::open(dialog, [&, dialog](int) { Dialogs::open(dialog, [&](int) { ++secondAnswers; }); });

    dialog->reject();
    settle();

    ASSERT_FALSE(guard.isNull());
    EXPECT_TRUE(guard->isVisible());
    guard->accept();
    settle();

    EXPECT_EQ(secondAnswers, 1);
    EXPECT_TRUE(guard.isNull());
}

TEST_F(NonBlockingDialogs, DestroyingTheParentAfterTheAnswerRunsNoCallback)
{
    int calls = 0;
    Dialogs::confirm(parent.get(), "Title", "Text", [&](bool) { ++calls; });

    onlyMessageBox()->button(QMessageBox::Yes)->click();
    parent.reset();
    settle();

    EXPECT_EQ(calls, 0);
}

TEST_F(NonBlockingDialogs, DeletingTheDialogAfterItFinishedRunsNoCallback)
{
    auto *dialog = new QInputDialog(parent.get());
    int calls = 0;
    Dialogs::open(dialog, [&](int) { ++calls; });

    dialog->accept();
    delete dialog;
    settle();

    EXPECT_EQ(calls, 0);
}

TEST_F(NonBlockingDialogs, TheCallbackRunsAfterTheBoxesWindowCloseHandling)
{
    int calls = 0;
    Dialogs::confirm(parent.get(), "Title", "Text", [&](bool) { ++calls; });

    onlyMessageBox()->close();
    EXPECT_EQ(calls, 0);
    settle();

    EXPECT_EQ(calls, 1);
}

TEST_F(NonBlockingDialogs, ACallbackMayDestroyTheParentAfterAWindowClose)
{
    Dialogs::confirm(parent.get(), "Title", "Text", [&](bool) { parent.reset(); });

    onlyMessageBox()->close();
    settle();

    EXPECT_EQ(parent, nullptr);
}

TEST_F(NonBlockingDialogs, WhileAliveForwardsOnlyWhileItsContextLives)
{
    auto *context = new QObject;
    int received = 0;
    const auto guarded = Dialogs::whileAlive(context, [&](int value) { received = value; });

    guarded(3);
    EXPECT_EQ(received, 3);

    delete context;
    guarded(5);
    EXPECT_EQ(received, 3);
}
} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

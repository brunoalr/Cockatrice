#include "client/settings/cache_settings.h"
#include "dialog_test_support.h"
#include "interface/shutdown_chain.h"
#include "interface/widgets/deck_editor/deck_state_manager.h"
#include "interface/widgets/tabs/unsaved_deck_guard.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QDir>
#include <QMessageBox>
#include <QPointer>
#include <gtest/gtest.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <libcockatrice/settings/deck_editor_settings.h>
#include <memory>
#include <optional>

RNG_Abstract *rng = nullptr; // NOSONAR: the client declares it extern and assigns it

namespace
{
using Location = UnsavedDeckGuard::DeckOpenLocation;

void settle()
{
    for (int pass = 0; pass < 3; ++pass) {
        DialogTest::settle();
    }
}

QList<QMessageBox *> openBoxes(const QWidget *parent)
{
    QList<QMessageBox *> open;
    for (QMessageBox *box : parent->findChildren<QMessageBox *>()) {
        if (box->isVisible()) {
            open.append(box);
        }
    }
    return open;
}

void click(const QWidget *parent, QMessageBox::StandardButton button)
{
    const auto boxes = openBoxes(parent);
    ASSERT_EQ(boxes.size(), 1);
    boxes.first()->button(button)->click();
    settle();
}

class ShutdownChainTest : public ::testing::Test // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    ShutdownChain chain;
    QObject owner;
    QStringList asked;
    std::optional<bool> finished;

    ShutdownChain::Step answering(const QString &name, bool yes)
    {
        return {&owner, [this, name, yes](const ShutdownChain::Answer &answer) {
                    asked.append(name);
                    answer(yes);
                }};
    }

    ShutdownChain::Step holding(const QString &name, QObject *stepOwner, ShutdownChain::Answer &held)
    {
        return {stepOwner, [this, name, &held](const ShutdownChain::Answer &answer) {
                    asked.append(name);
                    held = answer;
                }};
    }

    bool start(QList<ShutdownChain::Step> steps)
    {
        return chain.start(std::move(steps), [this](bool accepted) {
            EXPECT_FALSE(finished.has_value()) << "the chain finished twice";
            finished = accepted;
        });
    }
};

TEST_F(ShutdownChainTest, EveryStepSayingYesAcceptsTheShutdown)
{
    EXPECT_TRUE(start({answering("first", true), answering("second", true), answering("third", true)}));
    settle();

    EXPECT_EQ(asked, QStringList({"first", "second", "third"}));
    EXPECT_EQ(finished, true);
    EXPECT_EQ(chain.state(), ShutdownChain::State::Accepted);
}

TEST_F(ShutdownChainTest, StepsThatAnswerAtOnceDecideInsideStart)
{
    start({answering("first", true), answering("second", true)});

    EXPECT_EQ(finished, true);
    EXPECT_EQ(chain.state(), ShutdownChain::State::Accepted);
}

TEST_F(ShutdownChainTest, ANoGivenAtOnceRefusesInsideStart)
{
    start({answering("first", false)});

    EXPECT_EQ(finished, false);
    EXPECT_EQ(chain.state(), ShutdownChain::State::Idle);
}

TEST_F(ShutdownChainTest, AnAnswerGivenLaterDecidesWhenItArrives)
{
    ShutdownChain::Answer held;
    start({holding("first", &owner, held)});
    EXPECT_FALSE(finished.has_value());
    EXPECT_EQ(chain.state(), ShutdownChain::State::Confirming);

    held(true);

    EXPECT_EQ(finished, true);
    EXPECT_EQ(chain.state(), ShutdownChain::State::Accepted);
}

TEST_F(ShutdownChainTest, ANoInTheFirstStepAsksNothingMore)
{
    start({answering("first", false), answering("second", true), answering("third", true)});
    settle();

    EXPECT_EQ(asked, QStringList({"first"}));
    EXPECT_EQ(finished, false);
    EXPECT_EQ(chain.state(), ShutdownChain::State::Idle);
}

TEST_F(ShutdownChainTest, ANoInAMiddleStepAsksNothingMore)
{
    start({answering("first", true), answering("second", false), answering("third", true)});
    settle();

    EXPECT_EQ(asked, QStringList({"first", "second"}));
    EXPECT_EQ(finished, false);
    EXPECT_EQ(chain.state(), ShutdownChain::State::Idle);
}

TEST_F(ShutdownChainTest, ANoInTheLastStepRefusesTheShutdown)
{
    start({answering("first", true), answering("second", true), answering("third", false)});
    settle();

    EXPECT_EQ(asked, QStringList({"first", "second", "third"}));
    EXPECT_EQ(finished, false);
    EXPECT_EQ(chain.state(), ShutdownChain::State::Idle);
}

TEST_F(ShutdownChainTest, AnEmptyChainAcceptsTheShutdown)
{
    EXPECT_TRUE(start({}));
    settle();

    EXPECT_EQ(finished, true);
    EXPECT_EQ(chain.state(), ShutdownChain::State::Accepted);
}

TEST_F(ShutdownChainTest, AStepThatAnswersLaterHoldsBackTheNextOne)
{
    ShutdownChain::Answer held;
    start({holding("first", &owner, held), answering("second", true)});
    settle();

    EXPECT_EQ(asked, QStringList({"first"}));
    EXPECT_FALSE(finished.has_value());
    EXPECT_EQ(chain.state(), ShutdownChain::State::Confirming);

    held(true);
    settle();
    EXPECT_EQ(asked, QStringList({"first", "second"}));
    EXPECT_EQ(finished, true);
}

TEST_F(ShutdownChainTest, AStepWhoseOwnerIsDestroyedBeforeItAnswersCountsAsYes)
{
    auto stepOwner = std::make_unique<QObject>();
    ShutdownChain::Answer held;
    start({holding("first", stepOwner.get(), held), answering("second", true)});
    settle();

    stepOwner.reset();
    settle();
    EXPECT_EQ(asked, QStringList({"first", "second"}));
    EXPECT_EQ(finished, true);
}

TEST_F(ShutdownChainTest, AStepWhoseOwnerIsGoneIsNotAsked)
{
    auto stepOwner = std::make_unique<QObject>();
    ShutdownChain::Answer held;
    ShutdownChain::Step gone = holding("gone", stepOwner.get(), held);
    stepOwner.reset();

    start({answering("first", true), gone, answering("third", true)});
    settle();
    EXPECT_EQ(asked, QStringList({"first", "third"}));
    EXPECT_EQ(finished, true);
}

TEST_F(ShutdownChainTest, AStepAnsweringTwiceIsHeardOnce)
{
    ShutdownChain::Answer held;
    start({holding("first", &owner, held), answering("second", true)});

    held(true);
    held(false);
    settle();
    EXPECT_EQ(asked, QStringList({"first", "second"}));
    EXPECT_EQ(finished, true);
}

TEST_F(ShutdownChainTest, StartingWhileOneRunsIsRefused)
{
    ShutdownChain::Answer held;
    ASSERT_TRUE(start({holding("first", &owner, held)}));

    bool secondFinished = false;
    EXPECT_FALSE(chain.start({answering("second", true)}, [&secondFinished](bool) { secondFinished = true; }));
    held(true);
    settle();

    EXPECT_EQ(asked, QStringList({"first"}));
    EXPECT_EQ(finished, true);
    EXPECT_FALSE(secondFinished);
}

TEST_F(ShutdownChainTest, AnAcceptedShutdownCannotStartAgain)
{
    start({});
    settle();

    EXPECT_FALSE(chain.start({answering("again", true)}, [](bool) {}));
    EXPECT_TRUE(asked.isEmpty());
}

TEST_F(ShutdownChainTest, ARefusedShutdownCanBeAskedAgain)
{
    start({answering("first", false)});
    settle();
    finished.reset();

    EXPECT_TRUE(start({answering("again", true)}));
    settle();
    EXPECT_EQ(asked, QStringList({"first", "again"}));
    EXPECT_EQ(finished, true);
}

TEST_F(ShutdownChainTest, AnAnswerAfterTheChainIsGoneIsDropped)
{
    auto doomed = std::make_unique<ShutdownChain>();
    ShutdownChain::Answer held;
    bool reported = false;
    doomed->start({holding("first", &owner, held)}, [&reported](bool) { reported = true; });

    doomed.reset();
    held(true);
    settle();
    EXPECT_FALSE(reported);
}

class UnsavedDeckGuardTest : public ::testing::Test // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    std::unique_ptr<QWidget> tab = std::make_unique<QWidget>();
    DeckStateManager deck;
    int tabShown = 0;
    int saves = 0;
    std::optional<bool> saveResult = true; // empty: the save answers later, through pendingSave
    std::function<void(bool saved)> pendingSave;
    UnsavedDeckGuard guard{tab.get(), &deck, [this] { ++tabShown; },
                           [this](std::function<void(bool saved)> then) { save(std::move(then)); }};
    std::optional<bool> mayClose;
    std::optional<Location> location;
    DialogTest::NestedLoopWatchdog nestedLoopWatchdog;

    void SetUp() override
    {
        deck.setModified(true);
    }

    void TearDown() override
    {
        SettingsCache::instance().deckEditor().setOpenDeckInNewTab(false);
    }

    void save(std::function<void(bool saved)> then)
    {
        ++saves;
        if (!saveResult) {
            pendingSave = std::move(then);
            return;
        }
        if (*saveResult) {
            deck.setModified(false);
        }
        then(*saveResult);
    }

    void confirmClose()
    {
        guard.confirmClose([this](bool answer) {
            EXPECT_FALSE(mayClose.has_value()) << "answered twice";
            mayClose = answer;
        });
    }

    void confirmOpen(bool openInSameTabIfBlank = true)
    {
        guard.confirmOpen(openInSameTabIfBlank, [this](Location answer) {
            EXPECT_FALSE(location.has_value()) << "answered twice";
            location = answer;
        });
    }
};

TEST_F(UnsavedDeckGuardTest, ClosingAnUnchangedDeckAsksNothing)
{
    deck.setModified(false);
    confirmClose();

    EXPECT_EQ(mayClose, true);
    EXPECT_TRUE(openBoxes(tab.get()).isEmpty());
    EXPECT_EQ(tabShown, 0);
}

TEST_F(UnsavedDeckGuardTest, AnUnchangedDeckClosesEvenWithADialogOfTheTabOpen)
{
    deck.setModified(false);
    QDialog other(tab.get());
    other.show();

    confirmClose();

    EXPECT_EQ(mayClose, true);
}

TEST_F(UnsavedDeckGuardTest, ClosingAChangedDeckShowsTheTabAndAsksWithoutBlocking)
{
    confirmClose();

    EXPECT_FALSE(mayClose.has_value());
    EXPECT_EQ(tabShown, 1);
    const auto boxes = openBoxes(tab.get());
    ASSERT_EQ(boxes.size(), 1);
    const QMessageBox *box = boxes.first();
    // QMessageBox ignores its window title on macOS.
#ifndef Q_OS_MACOS
    EXPECT_EQ(box->windowTitle(), "Are you sure?");
#endif
    EXPECT_EQ(box->text(), "The decklist has been modified.\nDo you want to save the changes?");
    EXPECT_EQ(box->icon(), QMessageBox::Warning);
    EXPECT_EQ(box->standardButtons(), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    EXPECT_EQ(box->buttons().size(), 3);
    EXPECT_EQ(box->windowModality(), Qt::ApplicationModal);
}

TEST_F(UnsavedDeckGuardTest, SaveLetsTheTabCloseOnceTheDeckIsSaved)
{
    confirmClose();
    click(tab.get(), QMessageBox::Save);

    EXPECT_EQ(saves, 1);
    EXPECT_EQ(mayClose, true);
    EXPECT_FALSE(deck.isModified());
}

TEST_F(UnsavedDeckGuardTest, ASaveThatFailsKeepsTheTab)
{
    saveResult = false;
    confirmClose();
    click(tab.get(), QMessageBox::Save);

    EXPECT_EQ(saves, 1);
    EXPECT_EQ(mayClose, false);
}

TEST_F(UnsavedDeckGuardTest, TheCloseWaitsForASaveThatAnswersLater)
{
    saveResult.reset();
    confirmClose();
    click(tab.get(), QMessageBox::Save);
    EXPECT_FALSE(mayClose.has_value());

    pendingSave(true);
    EXPECT_EQ(mayClose, true);
}

TEST_F(UnsavedDeckGuardTest, DiscardLetsTheTabCloseWithoutSaving)
{
    confirmClose();
    click(tab.get(), QMessageBox::Discard);

    EXPECT_EQ(saves, 0);
    EXPECT_EQ(mayClose, true);
}

TEST_F(UnsavedDeckGuardTest, CancelKeepsTheTab)
{
    confirmClose();
    click(tab.get(), QMessageBox::Cancel);

    EXPECT_EQ(saves, 0);
    EXPECT_EQ(mayClose, false);
}

TEST_F(UnsavedDeckGuardTest, ClosingTheBoxKeepsTheTab)
{
    confirmClose();
    const auto boxes = openBoxes(tab.get());
    ASSERT_EQ(boxes.size(), 1);
    boxes.first()->close();
    settle();

    EXPECT_EQ(mayClose, false);
}

TEST_F(UnsavedDeckGuardTest, ASecondCloseWhileTheBoxIsOpenIsRefused)
{
    confirmClose();
    std::optional<bool> second;
    guard.confirmClose([&second](bool answer) { second = answer; });

    EXPECT_EQ(second, false);
    EXPECT_EQ(openBoxes(tab.get()).size(), 1);
    click(tab.get(), QMessageBox::Discard);
    EXPECT_EQ(mayClose, true);
}

TEST_F(UnsavedDeckGuardTest, TheAnswerIsDroppedWithTheTab)
{
    confirmClose();
    tab.reset();
    settle();

    EXPECT_FALSE(mayClose.has_value());
}

TEST_F(UnsavedDeckGuardTest, OpeningOverAnUnchangedDeckUsesThisTab)
{
    deck.setModified(false);
    confirmOpen();

    EXPECT_EQ(location, Location::SAME_TAB);
    EXPECT_TRUE(openBoxes(tab.get()).isEmpty());
}

TEST_F(UnsavedDeckGuardTest, TheNewTabSettingOpensWithoutAsking)
{
    SettingsCache::instance().deckEditor().setOpenDeckInNewTab(true);
    confirmOpen();

    EXPECT_EQ(location, Location::NEW_TAB);
    EXPECT_TRUE(openBoxes(tab.get()).isEmpty());
}

TEST_F(UnsavedDeckGuardTest, TheNewTabSettingReusesABlankTabWhenAllowed)
{
    SettingsCache::instance().deckEditor().setOpenDeckInNewTab(true);
    deck.setModified(false);
    confirmOpen(true);
    EXPECT_EQ(location, Location::SAME_TAB);

    location.reset();
    confirmOpen(false);
    EXPECT_EQ(location, Location::NEW_TAB);
}

TEST_F(UnsavedDeckGuardTest, OpeningOverAChangedDeckAlsoOffersANewTab)
{
    confirmOpen();

    EXPECT_FALSE(location.has_value());
    EXPECT_EQ(tabShown, 1);
    const auto boxes = openBoxes(tab.get());
    ASSERT_EQ(boxes.size(), 1);
    const QMessageBox *box = boxes.first();
    EXPECT_EQ(box->text(), "The decklist has been modified.\nDo you want to save the changes?");
    EXPECT_EQ(box->standardButtons(), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    ASSERT_EQ(box->buttons().size(), 4);
}

TEST_F(UnsavedDeckGuardTest, OpenInNewTabOpensInANewTab)
{
    confirmOpen();
    const auto boxes = openBoxes(tab.get());
    ASSERT_EQ(boxes.size(), 1);
    QAbstractButton *newTab = nullptr;
    for (QAbstractButton *button : boxes.first()->buttons()) {
        if (button->text() == "Open in new tab") {
            newTab = button;
        }
    }
    ASSERT_NE(newTab, nullptr);
    EXPECT_EQ(boxes.first()->buttonRole(newTab), QMessageBox::ApplyRole);
    newTab->click();
    settle();

    EXPECT_EQ(location, Location::NEW_TAB);
    EXPECT_EQ(saves, 0);
}

TEST_F(UnsavedDeckGuardTest, SaveOpensInThisTab)
{
    confirmOpen();
    click(tab.get(), QMessageBox::Save);

    EXPECT_EQ(saves, 1);
    EXPECT_EQ(location, Location::SAME_TAB);
}

TEST_F(UnsavedDeckGuardTest, ASaveThatFailsCancelsTheOpen)
{
    saveResult = false;
    confirmOpen();
    click(tab.get(), QMessageBox::Save);

    EXPECT_EQ(saves, 1);
    EXPECT_EQ(location, Location::CANCELLED);
}

TEST_F(UnsavedDeckGuardTest, DiscardOpensInThisTab)
{
    confirmOpen();
    click(tab.get(), QMessageBox::Discard);

    EXPECT_EQ(saves, 0);
    EXPECT_EQ(location, Location::SAME_TAB);
}

TEST_F(UnsavedDeckGuardTest, CancelCancelsTheOpen)
{
    confirmOpen();
    click(tab.get(), QMessageBox::Cancel);

    EXPECT_EQ(saves, 0);
    EXPECT_EQ(location, Location::CANCELLED);
}

TEST_F(UnsavedDeckGuardTest, ASecondOpenWhileTheBoxIsOpenIsCancelled)
{
    confirmOpen();
    std::optional<Location> second;
    guard.confirmOpen(true, [&second](Location answer) { second = answer; });

    EXPECT_EQ(second, Location::CANCELLED);
    EXPECT_EQ(openBoxes(tab.get()).size(), 1);
}

// Closes as MainWindow does, which cannot be built without most of the client.
class TwoPassWindow : public QWidget
{
public:
    ShutdownChain chain;
    std::function<QList<ShutdownChain::Step>()> steps;

    void closeForUpdate(std::function<void(bool closed)> then)
    {
        if (chain.state() != ShutdownChain::State::Idle) {
            then(false);
            return;
        }
        confirmShutdown(std::move(then));
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (chain.state() == ShutdownChain::State::Accepted) {
            event->accept();
            return;
        }
        confirmShutdown({});
        event->setAccepted(chain.state() == ShutdownChain::State::Accepted);
    }

private:
    void confirmShutdown(std::function<void(bool closed)> then)
    {
        chain.start(steps(), [this, then = std::move(then)](bool accepted) {
            if (accepted) {
                close();
            }
            if (then) {
                then(accepted);
            }
        });
    }
};

UnsavedDeckGuard::Save savingTo(DeckStateManager &deck)
{
    return [&deck](const std::function<void(bool saved)> &then) {
        deck.setModified(false);
        then(true);
    };
}

class WindowClose : public ::testing::Test // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    std::unique_ptr<TwoPassWindow> window = std::make_unique<TwoPassWindow>();
    DeckStateManager firstDeck;
    DeckStateManager secondDeck;
    QPointer<QWidget> firstTab = new QWidget(window.get());
    QPointer<QWidget> secondTab = new QWidget(window.get());
    UnsavedDeckGuard firstGuard{firstTab, &firstDeck, [] {}, savingTo(firstDeck)};
    UnsavedDeckGuard secondGuard{secondTab, &secondDeck, [] {}, savingTo(secondDeck)};
    DialogTest::NestedLoopWatchdog nestedLoopWatchdog;

    void SetUp() override
    {
        firstDeck.setModified(true);
        secondDeck.setModified(true);
        window->steps = [this] {
            return QList<ShutdownChain::Step>{
                {firstTab, [this](const ShutdownChain::Answer &answer) { firstGuard.confirmClose(answer); }},
                {secondTab, [this](const ShutdownChain::Answer &answer) { secondGuard.confirmClose(answer); }},
            };
        };
        window->show();
    }
};

TEST_F(WindowClose, AWindowWithNothingToAskClosesAtOnce)
{
    firstDeck.setModified(false);
    secondDeck.setModified(false);

    EXPECT_TRUE(window->close());
    EXPECT_FALSE(window->isVisible());
    EXPECT_EQ(window->chain.state(), ShutdownChain::State::Accepted);
}

TEST_F(WindowClose, CancelOnTheFirstDeckAsksNothingMore)
{
    window->close();
    EXPECT_EQ(openBoxes(firstTab).size(), 1);
    EXPECT_TRUE(openBoxes(secondTab).isEmpty());

    click(firstTab, QMessageBox::Cancel);
    EXPECT_TRUE(openBoxes(window.get()).isEmpty());
    EXPECT_TRUE(window->isVisible());
    EXPECT_TRUE(firstDeck.isModified());
    EXPECT_TRUE(secondDeck.isModified());
    EXPECT_EQ(window->chain.state(), ShutdownChain::State::Idle);
}

TEST_F(WindowClose, SaveOnTheFirstThenCancelOnTheSecondKeepsTheSaveAndTheWindow)
{
    window->close();
    click(firstTab, QMessageBox::Save);
    EXPECT_EQ(openBoxes(secondTab).size(), 1);
    click(secondTab, QMessageBox::Cancel);

    EXPECT_FALSE(firstDeck.isModified());
    EXPECT_TRUE(secondDeck.isModified());
    EXPECT_TRUE(window->isVisible());
    EXPECT_EQ(window->chain.state(), ShutdownChain::State::Idle);
}

TEST_F(WindowClose, AnsweringEveryDeckClosesTheWindow)
{
    window->close();
    click(firstTab, QMessageBox::Save);
    click(secondTab, QMessageBox::Discard);

    EXPECT_FALSE(firstDeck.isModified());
    EXPECT_FALSE(window->isVisible());
    EXPECT_EQ(window->chain.state(), ShutdownChain::State::Accepted);
}

TEST_F(WindowClose, ASecondCloseWhileAskingAsksNothingMore)
{
    window->close();
    window->close();

    EXPECT_EQ(openBoxes(window.get()).size(), 1);
    click(firstTab, QMessageBox::Discard);
    click(secondTab, QMessageBox::Discard);
    EXPECT_FALSE(window->isVisible());
}

TEST_F(WindowClose, ADeckTabDestroyedWhileAskingLetsTheWindowClose)
{
    window->close();
    ASSERT_EQ(openBoxes(firstTab).size(), 1);

    firstTab->deleteLater();
    settle();
    ASSERT_EQ(openBoxes(secondTab).size(), 1);
    click(secondTab, QMessageBox::Discard);
    EXPECT_FALSE(window->isVisible());
}

TEST_F(WindowClose, CloseForUpdateReportsClosedOnceTheWindowClosed)
{
    firstDeck.setModified(false);
    secondDeck.setModified(false);
    std::optional<bool> closed;
    window->closeForUpdate([&closed](bool answer) { closed = answer; });

    EXPECT_EQ(closed, true);
    EXPECT_FALSE(window->isVisible());
}

TEST_F(WindowClose, CloseForUpdateReportsClosedAfterEveryDeckWasAnswered)
{
    std::optional<bool> closed;
    window->closeForUpdate([&closed](bool answer) { closed = answer; });
    click(firstTab, QMessageBox::Save);
    EXPECT_FALSE(closed.has_value());
    click(secondTab, QMessageBox::Discard);

    EXPECT_EQ(closed, true);
    EXPECT_FALSE(window->isVisible());
}

TEST_F(WindowClose, CloseForUpdateReportsNotClosedOnCancel)
{
    std::optional<bool> closed;
    window->closeForUpdate([&closed](bool answer) { closed = answer; });
    click(firstTab, QMessageBox::Cancel);

    EXPECT_EQ(closed, false);
    EXPECT_TRUE(window->isVisible());
}

TEST_F(WindowClose, CloseForUpdateReportsNotClosedAtOnceWhileAShutdownIsAsking)
{
    window->close();
    std::optional<bool> closed;
    window->closeForUpdate([&closed](bool answer) { closed = answer; });

    EXPECT_EQ(closed, false);
    EXPECT_EQ(openBoxes(window.get()).size(), 1);
}
} // namespace

int main(int argc, char **argv)
{
    return DialogTest::run(argc, argv, QStringLiteral("deck-close-test"), [] {
        const int result = RUN_ALL_TESTS();

        QDir(SettingsCache::instance().getDataPath()).removeRecursively();
        QDir(SettingsCache::instance().getCachePath()).removeRecursively();
        return result;
    });
}

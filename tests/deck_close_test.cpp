#include "interface/shutdown_chain.h"

#include <QApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <memory>
#include <optional>

namespace
{
void settle()
{
    for (int pass = 0; pass < 3; ++pass) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
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

TEST_F(ShutdownChainTest, TheOutcomeArrivesFromTheEventLoop)
{
    start({answering("first", true)});

    EXPECT_FALSE(finished.has_value());
    EXPECT_EQ(chain.state(), ShutdownChain::State::Confirming);
    settle();
    EXPECT_EQ(finished, true);
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

TEST_F(ShutdownChainTest, StartingIsRefusedOnceTheOutcomeIsDecidedButNotYetReported)
{
    start({answering("first", true)});

    EXPECT_FALSE(chain.start({answering("second", true)}, [](bool) {}));
    settle();
    EXPECT_EQ(asked, QStringList({"first"}));
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
} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // A CI container's user has no home it can write to.
    const QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CockatriceTest"));
    QCoreApplication::setApplicationName(QStringLiteral("deck-close-test-%1").arg(QCoreApplication::applicationPid()));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

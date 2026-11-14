#include "client/settings/cache_settings.h"
#include "dialog_test_support.h"
#include "interface/widgets/dialogs/settings_close_check.h"
#include "interface/widgets/utility/non_blocking_dialogs.h"

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <libcockatrice/card/database/card_database.h>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/settings/paths_settings.h>

namespace
{
using DialogTest::settle;

const QString DatabaseQuestion = QStringLiteral("Your card database did not finish loading");
const QString DeckQuestion = QStringLiteral("The path to your deck directory is invalid.");
const QString PicturesQuestion = QStringLiteral("The path to your card pictures directory is invalid.");

class SettingsDialogs : public DialogTest::Fixture // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    void answer(const QString &textStart, QMessageBox::StandardButton button) const
    {
        const QMessageBox *box = onlyOpen<QMessageBox>();
        ASSERT_NE(box, nullptr);
        EXPECT_TRUE(box->text().startsWith(textStart)) << box->text().toStdString();
        box->button(button)->click();
        // The answer can close the dialog, whose own callback is queued in turn.
        settle();
        settle();
    }
};

class SettingsClose : public SettingsDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QTemporaryDir database;
    QTemporaryDir decks;
    QTemporaryDir pictures;
    QPointer<QDialog> dialog = new QDialog(window.get());
    QList<int> results;

    void SetUp() override
    {
        const QString cards = database.filePath("cards.xml");
        ASSERT_TRUE(QFile::copy(QStringLiteral(CARDDB_DATADIR "cards.xml"), cards));
        SettingsCache::instance().paths().setCardDatabasePath(cards);
        CardDatabaseManager::getInstance()->loadCardDatabases();
        SettingsCache::instance().paths().setDeckPath(decks.path());
        SettingsCache::instance().paths().setPicsPath(pictures.path());
        new SettingsCloseCheck(dialog.data());
        Dialogs::open(dialog.data(), [this](int result) { results.append(result); });
        settle();
    }

    void makeEverySettingUnusable() const
    {
        CardDatabaseManager::getInstance()->clear();
        SettingsCache::instance().paths().setDeckPath(decks.filePath("missing"));
        SettingsCache::instance().paths().setPicsPath(pictures.filePath("missing"));
    }

    bool close() const
    {
        const bool closed = dialog->close();
        settle();
        return closed;
    }

    void expectStillOpen() const
    {
        ASSERT_FALSE(dialog.isNull());
        EXPECT_TRUE(dialog->isVisible());
        EXPECT_TRUE(results.isEmpty());
        EXPECT_TRUE(openDialogs<QMessageBox>().isEmpty());
    }
};

TEST_F(SettingsClose, NothingToAskClosesAtOnce)
{
    EXPECT_TRUE(dialog->close());
    EXPECT_FALSE(dialog->isVisible());
    EXPECT_TRUE(openDialogs<QMessageBox>().isEmpty());

    settle();
    EXPECT_EQ(results, QList<int>{QDialog::Rejected});
}

TEST_F(SettingsClose, GoingBackAtAnyQuestionKeepsTheDialogOpenAndAsksNothingMore)
{
    const QStringList questions{DatabaseQuestion, DeckQuestion, PicturesQuestion};
    makeEverySettingUnusable();
    for (int goBackAt = 0; goBackAt < questions.size(); ++goBackAt) {
        EXPECT_FALSE(close());
        for (int earlier = 0; earlier < goBackAt; ++earlier) {
            answer(questions.at(earlier), QMessageBox::No);
        }
        answer(questions.at(goBackAt), QMessageBox::Yes);
        expectStillOpen();
    }
}

TEST_F(SettingsClose, NoToEveryQuestionClosesTheDialog)
{
    makeEverySettingUnusable();
    EXPECT_FALSE(close());
    answer(DatabaseQuestion, QMessageBox::No);
    answer(DeckQuestion, QMessageBox::No);
    EXPECT_TRUE(results.isEmpty());

    answer(PicturesQuestion, QMessageBox::No);
    EXPECT_EQ(results, QList<int>{QDialog::Rejected});
    EXPECT_TRUE(openDialogs<QMessageBox>().isEmpty());
}

TEST_F(SettingsClose, OnlyUnusableSettingsAreAskedAbout)
{
    SettingsCache::instance().paths().setPicsPath(pictures.filePath("missing"));
    EXPECT_FALSE(close());

    answer(PicturesQuestion, QMessageBox::No);
    EXPECT_EQ(results, QList<int>{QDialog::Rejected});
}

TEST_F(SettingsClose, ACloseWhileAskingIsRefused)
{
    makeEverySettingUnusable();
    EXPECT_FALSE(close());
    EXPECT_FALSE(close());

    answer(DatabaseQuestion, QMessageBox::Yes);
    expectStillOpen();
}

TEST_F(SettingsClose, ADialogDestroyedWhileAskingTakesItsQuestionWithIt)
{
    makeEverySettingUnusable();
    EXPECT_FALSE(close());
    ASSERT_EQ(openDialogs<QMessageBox>().size(), 1);

    dialog->deleteLater();
    settle();
    EXPECT_TRUE(dialog.isNull());
    EXPECT_TRUE(openDialogs<QMessageBox>().isEmpty());
    EXPECT_TRUE(results.isEmpty());
}
} // namespace

int main(int argc, char **argv)
{
    return DialogTest::run(argc, argv, QStringLiteral("settings-dialogs-test"), [] {
        CardDatabaseManager::setCardDatabasePathProvider(&SettingsCache::instance());

        const int result = RUN_ALL_TESTS();

        QDir(SettingsCache::instance().getDataPath()).removeRecursively();
        QDir(SettingsCache::instance().getCachePath()).removeRecursively();
        return result;
    });
}

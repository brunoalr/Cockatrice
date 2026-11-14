#include "client/settings/cache_settings.h"
#include "dialog_test_support.h"
#include "interface/card_picture_loader/card_picture_loader.h"
#include "interface/theme_manager.h"
#include "interface/widgets/dialogs/settings_close_check.h"
#include "interface/widgets/settings_page/deck_editor_settings_page.h"
#include "interface/widgets/settings_page/general_settings_page.h"
#include "interface/widgets/utility/non_blocking_dialogs.h"
#include "main.h"

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <algorithm>
#include <gtest/gtest.h>
#include <libcockatrice/card/database/card_database.h>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/settings/paths_settings.h>
#include <memory>

ThemeManager *themeManager = nullptr;                          // NOSONAR: the client declares it extern and assigns it
QSystemTrayIcon *trayIcon = nullptr;                           // NOSONAR: the client declares it extern and assigns it
const QString translationPrefix = QStringLiteral("cockatrice"); // NOSONAR: the client declares it extern
QString translationPath;                                       // NOSONAR: the client declares it extern

void PrintTo(const QString &text, std::ostream *out)
{
    *out << text.toStdString();
}

void CardPictureLoader::clearPixmapCache()
{
    // The test binary has no picture loader: nothing is cached.
}

void CardPictureLoader::clearNetworkCache()
{
    // The test binary has no picture loader: nothing is cached.
}

namespace
{
using DialogTest::settle;

const QString DatabaseQuestion = QStringLiteral("Your card database did not finish loading");
const QString DeckQuestion = QStringLiteral("The path to your deck directory is invalid.");
const QString PicturesQuestion = QStringLiteral("The path to your card pictures directory is invalid.");

class SettingsDialogs : public DialogTest::Fixture // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    template <typename Page> Page *addPage() const
    {
        auto page = std::make_unique<Page>();
        page->setParent(window.get());
        return page.release();
    }

    void answer(const QString &textPart, QMessageBox::StandardButton button) const
    {
        const QMessageBox *box = onlyOpen<QMessageBox>();
        ASSERT_NE(box, nullptr);
        EXPECT_TRUE(box->text().contains(textPart)) << box->text().toStdString();
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
class PathChoosers : public SettingsDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QTemporaryDir chosen;
    QString deckPathBefore = QDir::tempPath();

    void SetUp() override
    {
        SettingsCache::instance().paths().setDeckPath(deckPathBefore);
    }

    void choose(QWidget *page, const char *chooser, const QString &path) const
    {
        QMetaObject::invokeMethod(page, chooser);
        settle();
        QFileDialog *files = onlyOpen<QFileDialog>();
        ASSERT_NE(files, nullptr);
        DialogTest::answerFileDialog(files, path);
        settle();
    }

    static bool showsPath(const QWidget *page, const QString &path)
    {
        const auto edits = page->findChildren<QLineEdit *>();
        return std::any_of(edits.cbegin(), edits.cend(), [&path](const QLineEdit *edit) { return edit->text() == path; });
    }
};

TEST_F(PathChoosers, AnAcceptedDirectoryIsApplied)
{
    auto *page = addPage<GeneralSettingsPage>();
    choose(page, "deckPathButtonClicked", chosen.path());

    EXPECT_EQ(SettingsCache::instance().paths().getDeckPath(), chosen.path());
    EXPECT_TRUE(showsPath(page, chosen.path()));
}

TEST_F(PathChoosers, ACancelledChoiceChangesNothing)
{
    auto *page = addPage<GeneralSettingsPage>();
    choose(page, "deckPathButtonClicked", QString());

    EXPECT_EQ(SettingsCache::instance().paths().getDeckPath(), deckPathBefore);
    EXPECT_FALSE(showsPath(page, chosen.path()));
}

TEST_F(PathChoosers, APathInsideTheProgramDirectoryIsRefusedWithAWarning)
{
    auto *page = addPage<GeneralSettingsPage>();
    choose(page, "deckPathButtonClicked", QCoreApplication::applicationDirPath());

    answer(QStringLiteral("is inside the Cockatrice program directory"), QMessageBox::Ok);
    EXPECT_EQ(SettingsCache::instance().paths().getDeckPath(), deckPathBefore);
    EXPECT_TRUE(openDialogs<QDialog>().isEmpty());
}

TEST_F(PathChoosers, AnAcceptedFileIsApplied)
{
    auto *page = addPage<GeneralSettingsPage>();
    QFile tokens(chosen.filePath("tokens.xml"));
    ASSERT_TRUE(tokens.open(QIODevice::WriteOnly));
    tokens.close();
    choose(page, "tokenDatabasePathButtonClicked", tokens.fileName());

    EXPECT_EQ(SettingsCache::instance().getTokenDatabasePath(), tokens.fileName());
}

TEST_F(PathChoosers, TheSpoilerFileGoesIntoTheChosenDirectory)
{
    auto *page = addPage<DeckEditorSettingsPage>();
    choose(page, "spoilerPathButtonClicked", chosen.path());

    EXPECT_EQ(SettingsCache::instance().getSpoilerCardDatabasePath(), chosen.filePath("spoiler.xml"));
}
} // namespace

int main(int argc, char **argv)
{
    return DialogTest::run(argc, argv, QStringLiteral("settings-dialogs-test"), [] {
        ThemeManager theme;
        themeManager = &theme;
        CardDatabaseManager::setCardDatabasePathProvider(&SettingsCache::instance());

        const int result = RUN_ALL_TESTS();

        QDir(SettingsCache::instance().getDataPath()).removeRecursively();
        QDir(SettingsCache::instance().getCachePath()).removeRecursively();
        return result;
    });
}

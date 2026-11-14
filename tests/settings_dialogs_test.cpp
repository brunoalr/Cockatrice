#include "client/settings/cache_settings.h"
#include "client/settings/shortcuts_settings.h"
#include "dialog_test_support.h"
#include "interface/card_picture_loader/card_picture_loader.h"
#include "interface/palette_editor/color_button.h"
#include "interface/theme_manager.h"
#include "interface/widgets/dialogs/settings_close_check.h"
#include "interface/widgets/settings_page/deck_editor_settings_page.h"
#include "interface/widgets/settings_page/general_settings_page.h"
#include "interface/widgets/settings_page/messages_settings_page.h"
#include "interface/widgets/settings_page/shortcut_settings_page.h"
#include "interface/widgets/utility/non_blocking_dialogs.h"
#include "main.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <algorithm>
#include <gtest/gtest.h>
#include <libcockatrice/card/database/card_database.h>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <libcockatrice/settings/cards_display_settings.h>
#include <libcockatrice/settings/download_settings.h>
#include <libcockatrice/settings/message_settings.h>
#include <libcockatrice/settings/paths_settings.h>
#include <libcockatrice/utility/string_limits.h>
#include <memory>

ThemeManager *themeManager = nullptr;                           // NOSONAR: the client declares it extern and assigns it
RNG_Abstract *rng = nullptr;                                    // NOSONAR: the client declares it extern and assigns it
QSystemTrayIcon *trayIcon = nullptr;                            // NOSONAR: the client declares it extern and assigns it
const QString translationPrefix = QStringLiteral("cockatrice"); // NOSONAR: the client declares it extern
QString translationPath;                                        // NOSONAR: the client declares it extern

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

    QInputDialog *openPrompt(QObject *page, const char *action) const
    {
        QMetaObject::invokeMethod(page, action);
        settle();
        return onlyOpen<QInputDialog>();
    }

    static void accept(QInputDialog *prompt)
    {
        prompt->accept();
        settle();
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
        return std::any_of(edits.cbegin(), edits.cend(),
                           [&path](const QLineEdit *edit) { return edit->text() == path; });
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
class CardLanguagePrompt : public SettingsDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    int updateRequests = 0;

    void SetUp() override
    {
        SettingsCache::instance().cardsDisplay().setCardLang("en");
    }

    void chooseAnotherLanguage()
    {
        auto *page = addPage<GeneralSettingsPage>();
        QObject::connect(page, &GeneralSettingsPage::cardDatabaseUpdateRequested, page, [this] { ++updateRequests; });
        for (QComboBox *box : page->findChildren<QComboBox *>()) {
            if (box->itemData(0) == "en" && box->count() > 1) {
                box->setCurrentIndex(1);
            }
        }
        settle();
        EXPECT_NE(SettingsCache::instance().cardsDisplay().getCardLang(), "en");
    }
};

TEST_F(CardLanguagePrompt, YesRunsOracle)
{
    chooseAnotherLanguage();
    EXPECT_EQ(updateRequests, 0);

    answer("Run Oracle now?", QMessageBox::Yes);
    EXPECT_EQ(updateRequests, 1);
}

TEST_F(CardLanguagePrompt, NoKeepsTheLanguageWithoutRunningOracle)
{
    chooseAnotherLanguage();
    answer("Run Oracle now?", QMessageBox::No);

    EXPECT_EQ(updateRequests, 0);
    EXPECT_NE(SettingsCache::instance().cardsDisplay().getCardLang(), "en");
}

class ShortcutPrompts : public SettingsDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    const QString shortcut = QStringLiteral("MainWindow/aConnect");
    QWidget *page = addPage<ShortcutSettingsPage>();

    void SetUp() override
    {
        SettingsCache::instance().shortcuts().setShortcuts(shortcut, QStringLiteral("Ctrl+K"));
    }

    QString current() const
    {
        return SettingsCache::instance().shortcuts().getShortcutString(shortcut);
    }

    void ask(const char *action, const QString &question, QMessageBox::StandardButton button) const
    {
        QMetaObject::invokeMethod(page, action);
        settle();
        EXPECT_EQ(current(), "Ctrl+K");
        answer(question, button);
    }
};

TEST_F(ShortcutPrompts, YesRestoresTheDefaultShortcuts)
{
    ask("resetShortcuts", "Do you really want to restore all default shortcuts?", QMessageBox::Yes);
    EXPECT_EQ(current(), "Ctrl+L");
}

TEST_F(ShortcutPrompts, NoKeepsTheShortcutsWhenAskedToRestore)
{
    ask("resetShortcuts", "Do you really want to restore all default shortcuts?", QMessageBox::No);
    EXPECT_EQ(current(), "Ctrl+K");
}

TEST_F(ShortcutPrompts, YesClearsEveryShortcut)
{
    ask("clearShortcuts", "Do you really want to clear all shortcuts?", QMessageBox::Yes);
    EXPECT_EQ(current(), "");
}

TEST_F(ShortcutPrompts, NoKeepsTheShortcutsWhenAskedToClear)
{
    ask("clearShortcuts", "Do you really want to clear all shortcuts?", QMessageBox::No);
    EXPECT_EQ(current(), "Ctrl+K");
}

class UrlPrompts : public SettingsDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    const QString first = QStringLiteral("https://example.com/first/!name!");
    const QString second = QStringLiteral("https://example.org/second/!name!");
    QWidget *page = nullptr;
    QListWidget *urls = nullptr;

    void SetUp() override
    {
        SettingsCache::instance().downloads().setDownloadUrls({first, second});
        SettingsCache::instance().downloads().setHostRequestLimits({});
        page = addPage<DeckEditorSettingsPage>();
        urls = page->findChild<QListWidget *>();
    }

    static QStringList stored()
    {
        return SettingsCache::instance().downloads().getAllURLs();
    }

    static QHash<QString, int> limits()
    {
        return SettingsCache::instance().downloads().getHostRequestLimits();
    }
};

TEST_F(UrlPrompts, AnAcceptedUrlIsAdded)
{
    QInputDialog *prompt = openPrompt(page, "actAddURL");
    ASSERT_NE(prompt, nullptr);
    prompt->setTextValue("https://example.net/!name!");
    accept(prompt);

    EXPECT_EQ(stored(), (QStringList{first, second, "https://example.net/!name!"}));
    EXPECT_EQ(urls->count(), 3);
}

TEST_F(UrlPrompts, ACancelledUrlIsNotAdded)
{
    QInputDialog *prompt = openPrompt(page, "actAddURL");
    ASSERT_NE(prompt, nullptr);
    prompt->reject();
    settle();

    EXPECT_EQ(stored(), (QStringList{first, second}));
    EXPECT_EQ(urls->count(), 2);
}

TEST_F(UrlPrompts, AnEditChangesTheUrlItWasOpenedOn)
{
    urls->setCurrentRow(0);
    QInputDialog *prompt = openPrompt(page, "actEditURL");
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->textValue(), first);

    urls->setCurrentRow(1);
    prompt->setTextValue("https://example.net/!name!");
    accept(prompt);

    EXPECT_EQ(stored(), (QStringList{"https://example.net/!name!", second}));
}

TEST_F(UrlPrompts, ResettingTheUrlsSaysSoWithoutBlocking)
{
    QMetaObject::invokeMethod(page, "resetDownloadedURLsButtonClicked");
    settle();
    EXPECT_NE(stored(), (QStringList{first, second}));

    answer("Download URLs have been reset.", QMessageBox::Ok);
    EXPECT_EQ(urls->count(), stored().size());
}

TEST_F(UrlPrompts, ARateLimitNeedsASelectedUrl)
{
    QMetaObject::invokeMethod(page, "actAdjustRateLimit");
    settle();

    answer("Select a URL in the list first.", QMessageBox::Ok);
    EXPECT_TRUE(openDialogs<QInputDialog>().isEmpty());
}

TEST_F(UrlPrompts, ARateLimitNeedsAUrlWithAHost)
{
    SettingsCache::instance().downloads().setDownloadUrls({"!name!"});
    page = addPage<DeckEditorSettingsPage>();
    page->findChild<QListWidget *>()->setCurrentRow(0);
    QMetaObject::invokeMethod(page, "actAdjustRateLimit");
    settle();

    answer("The selected URL does not have a valid host.", QMessageBox::Ok);
    EXPECT_TRUE(openDialogs<QInputDialog>().isEmpty());
}

TEST_F(UrlPrompts, AnAcceptedRateLimitIsStoredForTheHostItWasOpenedOn)
{
    urls->setCurrentRow(0);
    QInputDialog *prompt = openPrompt(page, "actAdjustRateLimit");
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->intValue(), DownloadSettings::DEFAULT_HOST_REQUEST_LIMIT);
    EXPECT_EQ(prompt->intMinimum(), DownloadSettings::MIN_HOST_REQUEST_LIMIT);
    EXPECT_EQ(prompt->intMaximum(), DownloadSettings::DEFAULT_HOST_REQUEST_LIMIT);

    urls->setCurrentRow(1);
    SettingsCache::instance().downloads().setHostRequestLimits({{"example.org", 2}});
    prompt->setIntValue(3);
    accept(prompt);

    EXPECT_EQ(limits(), (QHash<QString, int>{{"example.org", 2}, {"example.com", 3}}));
}

TEST_F(UrlPrompts, ACancelledRateLimitChangesNothing)
{
    urls->setCurrentRow(0);
    QInputDialog *prompt = openPrompt(page, "actAdjustRateLimit");
    ASSERT_NE(prompt, nullptr);
    prompt->setIntValue(3);
    prompt->reject();
    settle();

    EXPECT_TRUE(limits().isEmpty());
}
class MessagePrompts : public SettingsDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QWidget *page = nullptr;
    QListWidget *messages = nullptr;

    void SetUp() override
    {
        SettingsCache::instance().messages().setCount(2);
        SettingsCache::instance().messages().setMessageAt(0, "gg");
        SettingsCache::instance().messages().setMessageAt(1, "brb");
        page = addPage<MessagesSettingsPage>();
        messages = page->findChild<QListWidget *>();
    }

    static QStringList stored()
    {
        QStringList texts;
        for (int index = 0; index < SettingsCache::instance().messages().getCount(); ++index) {
            texts.append(SettingsCache::instance().messages().getMessageAt(index));
        }
        return texts;
    }

    static void type(const QInputDialog *prompt, const QString &text)
    {
        prompt->findChild<QLineEdit *>()->setText(text);
    }
};

TEST_F(MessagePrompts, AnAcceptedMessageIsAdded)
{
    QInputDialog *prompt = openPrompt(page, "actAdd");
    ASSERT_NE(prompt, nullptr);
    type(prompt, "glhf");
    accept(prompt);

    EXPECT_EQ(stored(), (QStringList{"gg", "brb", "glhf"}));
    EXPECT_EQ(messages->count(), 3);
}

TEST_F(MessagePrompts, ACancelledMessageIsNotAdded)
{
    QInputDialog *prompt = openPrompt(page, "actAdd");
    ASSERT_NE(prompt, nullptr);
    type(prompt, "glhf");
    prompt->reject();
    settle();

    EXPECT_EQ(stored(), (QStringList{"gg", "brb"}));
}

TEST_F(MessagePrompts, AMessageIsCutToTheTextLimit)
{
    QInputDialog *prompt = openPrompt(page, "actAdd");
    ASSERT_NE(prompt, nullptr);
    type(prompt, QString(MAX_TEXT_LENGTH + 10, 'a'));
    accept(prompt);

    EXPECT_EQ(stored().value(2), QString(MAX_TEXT_LENGTH, 'a'));
}

TEST_F(MessagePrompts, AnEditChangesTheMessageItWasOpenedOn)
{
    messages->setCurrentRow(0);
    QInputDialog *prompt = openPrompt(page, "actEdit");
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->textValue(), "gg");
    EXPECT_EQ(prompt->findChild<QLineEdit *>()->maxLength(), MAX_TEXT_LENGTH);

    messages->setCurrentRow(1);
    type(prompt, "wp");
    accept(prompt);

    EXPECT_EQ(stored(), (QStringList{"wp", "brb"}));
}

TEST_F(MessagePrompts, ACancelledEditKeepsTheMessage)
{
    messages->setCurrentRow(0);
    QInputDialog *prompt = openPrompt(page, "actEdit");
    ASSERT_NE(prompt, nullptr);
    type(prompt, "wp");
    prompt->reject();
    settle();

    EXPECT_EQ(stored(), (QStringList{"gg", "brb"}));
}
class ColourPicker : public SettingsDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    ColorButton *button = new ColorButton(window.get());
    QList<QColor> changes;

    void SetUp() override
    {
        button->setColor(Qt::blue);
        QObject::connect(button, &ColorButton::colorChanged, button,
                         [this](const QColor &color) { changes.append(color); });
    }

    QColorDialog *pick() const
    {
        button->click();
        settle();
        return onlyOpen<QColorDialog>();
    }
};

TEST_F(ColourPicker, AnAcceptedColourIsSet)
{
    QColorDialog *dialog = pick();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->currentColor(), QColor(Qt::blue));
    EXPECT_TRUE(dialog->testOption(QColorDialog::ShowAlphaChannel));

    dialog->setCurrentColor(Qt::red);
    dialog->accept();
    settle();
    EXPECT_EQ(button->getColor(), QColor(Qt::red));
    EXPECT_EQ(changes, QList<QColor>{QColor(Qt::red)});
}

TEST_F(ColourPicker, ACancelledPickKeepsTheColour)
{
    QColorDialog *dialog = pick();
    ASSERT_NE(dialog, nullptr);

    dialog->setCurrentColor(Qt::red);
    dialog->reject();
    settle();
    EXPECT_EQ(button->getColor(), QColor(Qt::blue));
    EXPECT_TRUE(changes.isEmpty());
}
class ToolButtonMenu : public SettingsDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QToolButton *button = new QToolButton(window.get());
    QMenu *menu = new QMenu(window.get());

    void SetUp() override
    {
        menu->addAction("Clear chat");
        window->show();
        Dialogs::showMenuOnClick(button, menu);
    }

    void TearDown() override
    {
        menu->close();
        settle();
    }
};

TEST_F(ToolButtonMenu, AClickShowsTheMenuBelowTheButton)
{
    QTest::mouseClick(button, Qt::LeftButton);
    settle();

    EXPECT_TRUE(menu->isVisible());
    EXPECT_EQ(menu->pos(), button->mapToGlobal(QPoint(0, button->height())));
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

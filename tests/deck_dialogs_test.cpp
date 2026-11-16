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

#include "client/settings/cache_settings.h"
#include "dialog_test_support.h"
#include "interface/card_picture_loader/card_picture_loader.h"
#include "interface/deck_loader/deck_loader.h"
#include "interface/theme_manager.h"
#include "interface/widgets/deck_editor/card_database_view.h"
#include "interface/widgets/deck_editor/deck_state_manager.h"
#include "interface/widgets/deck_editor/deck_zone_dialog.h"
#include "interface/widgets/dialogs/dlg_convert_deck_to_cod_format.h"
#include "interface/widgets/dialogs/dlg_default_tags_editor.h"
#include "interface/widgets/dialogs/dlg_edit_tokens.h"
#include "interface/widgets/dialogs/dlg_load_deck.h"
#include "interface/widgets/dialogs/dlg_load_deck_from_website.h"
#include "interface/widgets/dialogs/override_printing_warning.h"
#include "interface/widgets/tabs/api/commander_spellbook/commander_bracket_widget.h"
#include "interface/widgets/tabs/deck_save_prompt.h"
#include "interface/widgets/utility/non_blocking_dialogs.h"
#include "interface/widgets/visual_deck_storage/deck_preview/deck_preview_tag_dialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <cstring>
#include <gtest/gtest.h>
#include <libcockatrice/card/card_info.h>
#include <libcockatrice/card/database/card_database.h>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/card/database/card_database_querier.h>
#include <libcockatrice/deck_list/deck_file_format.h>
#include <libcockatrice/deck_list/tree/inner_deck_list_node.h>
#include <libcockatrice/models/database/card_database_display_model.h>
#include <libcockatrice/models/database/card_database_model.h>
#include <libcockatrice/models/deck_list/deck_list_model.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <libcockatrice/settings/cards_display_settings.h>
#include <libcockatrice/settings/deck_editor_settings.h>
#include <libcockatrice/settings/layouts_settings.h>
#include <libcockatrice/settings/paths_settings.h>
#include <libcockatrice/settings/visual_deck_storage_settings.h>
#include <optional>
#include <version_string.h>

ThemeManager *themeManager = nullptr; // NOSONAR: the client declares it extern and assigns it
RNG_Abstract *rng = nullptr;          // NOSONAR: the client declares it extern and assigns it

namespace
{
struct PictureCacheClears
{
    int pixmap = 0;
    int network = 0;
};

PictureCacheClears &pictureCacheClears()
{
    static PictureCacheClears clears;
    return clears;
}
} // namespace

void CardPictureLoader::clearPixmapCache()
{
    ++pictureCacheClears().pixmap;
}

void CardPictureLoader::clearNetworkCache()
{
    ++pictureCacheClears().network;
}

namespace
{
using DialogTest::settle;

const QString TappedOutDeck = QStringLiteral("https://tappedout.net/mtg-decks/some-deck/");

QPushButton *buttonWithText(const QWidget *parent, const QString &text)
{
    for (QPushButton *button : parent->findChildren<QPushButton *>()) {
        if (button->text() == text) {
            return button;
        }
    }
    ADD_FAILURE() << "no button reads " << text.toStdString();
    return nullptr;
}

QLineEdit *lineEditWithPlaceholder(const QWidget *parent, const QString &placeholderStart)
{
    for (QLineEdit *edit : parent->findChildren<QLineEdit *>()) {
        if (edit->placeholderText().startsWith(placeholderStart)) {
            return edit;
        }
    }
    ADD_FAILURE() << "no line edit has a placeholder starting with " << placeholderStart.toStdString();
    return nullptr;
}

QAction *actionWithText(const QWidget *parent, const QString &text)
{
    for (QAction *action : parent->findChildren<QAction *>()) {
        if (action->text() == text) {
            return action;
        }
    }
    ADD_FAILURE() << "no action reads " << text.toStdString();
    return nullptr;
}

class DeckDialogs : public DialogTest::Fixture // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    void clickInOnlyBox(QMessageBox::StandardButton button, const QString &expectedText = {}) const
    {
        const QMessageBox *box = onlyOpen<QMessageBox>();
        ASSERT_NE(box, nullptr);
        if (!expectedText.isEmpty()) {
            EXPECT_EQ(box->text(), expectedText);
        }
        box->button(button)->click();
        settle();
    }
};

class FakeReply : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        QIODevice::open(QIODevice::ReadOnly);
    }

    void answer(const QByteArray &data)
    {
        body = data;
        finish();
    }

    void fail(const QString &message)
    {
        setError(QNetworkReply::ConnectionRefusedError, message);
        finish();
    }

    void abort() override
    {
        // The test decides when a reply finishes: there is no transfer to cancel.
    }

    qint64 bytesAvailable() const override
    {
        return body.size() - offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 count = qMin(maxSize, body.size() - offset);
        std::memcpy(data, body.constData() + offset, count);
        offset += count;
        return count;
    }

private:
    QByteArray body;
    qint64 offset = 0;

    void finish()
    {
        setFinished(true);
        emit finished();
    }
};

class FakeNetwork : public QNetworkAccessManager
{
public:
    QList<QPointer<FakeReply>> replies;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        auto *reply = new FakeReply(request, this);
        replies.append(reply);
        return reply;
    }
};

class WebsiteImport : public DeckDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    FakeNetwork network;
    QPointer<DlgLoadDeckFromWebsite> dialog;
    QList<int> results;
    QStringList importedCards;

    void SetUp() override
    {
        QApplication::clipboard()->clear();
        dialog = new DlgLoadDeckFromWebsite(window.get(), &network);
        Dialogs::open(dialog.data(), [this](int result) {
            results.append(result);
            importedCards = dialog->getDeck().getCardList();
        });
        settle();
    }

    void requestDeck(const QString &url) const
    {
        dialog->findChild<QLineEdit *>()->setText(url);
        okButton()->click();
        settle();
    }

    QPushButton *okButton() const
    {
        return dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    }
};

TEST_F(WebsiteImport, AnUnsupportedAddressWarnsAndKeepsTheDialogOpen)
{
    requestDeck("https://example.com/decks/1");
    clickInOnlyBox(QMessageBox::Ok);

    ASSERT_FALSE(dialog.isNull());
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_TRUE(results.isEmpty());
    EXPECT_TRUE(network.replies.isEmpty());
}

TEST_F(WebsiteImport, WhileARequestRunsOkIsDisabledAndASecondAcceptStartsNothing)
{
    requestDeck(TappedOutDeck);
    ASSERT_EQ(network.replies.size(), 1);
    EXPECT_FALSE(okButton()->isEnabled());
    const QNetworkRequest request = network.replies.first()->request();
    EXPECT_EQ(request.url().toString().toStdString(), "https://tappedout.net/mtg-decks/some-deck/?fmt=txt");
    EXPECT_EQ(request.header(QNetworkRequest::UserAgentHeader).toString(),
              QStringLiteral("Cockatrice %1").arg(VERSION_STRING));

    dialog->accept();
    settle();

    EXPECT_EQ(network.replies.size(), 1);
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_TRUE(results.isEmpty());
}

TEST_F(WebsiteImport, ANetworkErrorWarnsAndLeavesTheDialogUsable)
{
    requestDeck(TappedOutDeck);
    ASSERT_EQ(network.replies.size(), 1);
    network.replies.first()->fail("connection refused");
    settle();

    clickInOnlyBox(QMessageBox::Ok, "Network error: connection refused");
    ASSERT_FALSE(dialog.isNull());
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_TRUE(okButton()->isEnabled());
    EXPECT_TRUE(results.isEmpty());
}

TEST_F(WebsiteImport, AnEmptyReplyWarnsAndLeavesTheDialogUsable)
{
    requestDeck(TappedOutDeck);
    ASSERT_EQ(network.replies.size(), 1);
    network.replies.first()->answer({});
    settle();

    clickInOnlyBox(QMessageBox::Ok, "Received empty deck data.");
    ASSERT_FALSE(dialog.isNull());
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_TRUE(okButton()->isEnabled());
    EXPECT_TRUE(results.isEmpty());
}

TEST_F(WebsiteImport, AGoodReplyAcceptsTheDialogOnce)
{
    requestDeck(TappedOutDeck);
    ASSERT_EQ(network.replies.size(), 1);
    network.replies.first()->answer("1 Island\n");
    settle();

    EXPECT_EQ(results, QList<int>{QDialog::Accepted});
    EXPECT_EQ(importedCards, QStringList{"Island"});
    EXPECT_TRUE(openDialogs<QMessageBox>().isEmpty());
}

class ZonePrompts : public DeckDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    DeckZoneDialog *openZoneDialog() const
    {
        settle();
        return onlyOpen<DeckZoneDialog>();
    }

    static void answer(const DeckZoneDialog *dialog, const QString &name)
    {
        dialog->findChild<QLineEdit *>()->setText(name);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        settle();
    }

    static void cancel(DeckZoneDialog *dialog)
    {
        dialog->reject();
        settle();
    }
};

TEST_F(ZonePrompts, AnAcceptedNewZoneReportsItsNameAndBoard)
{
    std::optional<QStringList> reported;
    DeckZoneDialog::promptForNewZone(window.get(), DECK_ZONE_SIDE, {},
                                     [&reported](const QString &boardName, const QString &zoneName) {
                                         reported = QStringList{boardName, zoneName};
                                     });
    DeckZoneDialog *dialog = openZoneDialog();
    ASSERT_NE(dialog, nullptr);
    EXPECT_FALSE(reported.has_value());

    answer(dialog, " Ramp ");
    EXPECT_EQ(reported, (QStringList{DECK_ZONE_SIDE, "Ramp"}));
}

TEST_F(ZonePrompts, ACancelledNewZoneReportsNothing)
{
    bool reported = false;
    DeckZoneDialog::promptForNewZone(window.get(), {}, {},
                                     [&reported](const QString &, const QString &) { reported = true; });
    DeckZoneDialog *dialog = openZoneDialog();
    ASSERT_NE(dialog, nullptr);

    cancel(dialog);
    EXPECT_FALSE(reported);
    EXPECT_TRUE(openDialogs<DeckZoneDialog>().isEmpty());
}

TEST_F(ZonePrompts, AnAcceptedRenameReportsTheNewName)
{
    std::optional<QString> reported;
    DeckZoneDialog::promptForRename(window.get(), "Ramp", {},
                                    [&reported](const QString &newName) { reported = newName; });
    DeckZoneDialog *dialog = openZoneDialog();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->getZoneName(), "Ramp");

    answer(dialog, "Removal");
    EXPECT_EQ(reported, "Removal");
}

TEST_F(ZonePrompts, ACancelledRenameReportsNothing)
{
    bool reported = false;
    DeckZoneDialog::promptForRename(window.get(), "Ramp", {}, [&reported](const QString &) { reported = true; });
    DeckZoneDialog *dialog = openZoneDialog();
    ASSERT_NE(dialog, nullptr);

    cancel(dialog);
    EXPECT_FALSE(reported);
}

TEST_F(ZonePrompts, TheDeckGetsTheZoneOnlyOnceTheNameIsAccepted)
{
    DeckStateManager deck;
    QStringList created;
    const auto createZone = [this, &deck, &created] {
        DeckZoneDialog::promptToCreateZone(window.get(), &deck, DECK_ZONE_MAIN,
                                           [&created](const QString &zoneName) { created.append(zoneName); });
        return openZoneDialog();
    };

    DeckZoneDialog *dialog = createZone();
    ASSERT_NE(dialog, nullptr);
    cancel(dialog);
    EXPECT_TRUE(deck.getModel()->getCustomZoneNames(DECK_ZONE_MAIN).isEmpty());
    EXPECT_TRUE(created.isEmpty());

    dialog = createZone();
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(deck.getModel()->getCustomZoneNames(DECK_ZONE_MAIN).isEmpty());
    answer(dialog, "Ramp");
    EXPECT_EQ(deck.getModel()->getCustomZoneNames(DECK_ZONE_MAIN), QStringList{"Ramp"});
    EXPECT_EQ(created, QStringList{"Ramp"});
}

class TagDialogs : public DeckDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    void SetUp() override
    {
        SettingsCache::instance().visualDeckStorage().setVisualDeckStorageDefaultTagsList({"Aggro"});
    }

    QWidget *openDefaultTagsEditor() const
    {
        auto *editor = new DlgDefaultTagsEditor(window.get());
        editor->show();
        return editor;
    }

    QWidget *openDeckTagDialog() const
    {
        auto *dialog = new DeckPreviewTagDialog({"Combo"}, {}, window.get());
        dialog->show();
        return dialog;
    }

    void expectWarningKeepsDialogOpen(const QWidget *dialog, const QString &text) const
    {
        EXPECT_TRUE(dialog->isVisible());
        clickInOnlyBox(QMessageBox::Ok, text);
        EXPECT_TRUE(dialog->isVisible());
        EXPECT_TRUE(openDialogs<QMessageBox>().isEmpty());
    }
};

TEST_F(TagDialogs, TheDefaultTagsEditorWarnsAboutAnEmptyTag)
{
    const QWidget *editor = openDefaultTagsEditor();
    buttonWithText(editor, "Add")->click();
    settle();

    expectWarningKeepsDialogOpen(editor, "Tag name cannot be empty!");
}

TEST_F(TagDialogs, TheDefaultTagsEditorWarnsAboutADuplicateTag)
{
    const QWidget *editor = openDefaultTagsEditor();
    lineEditWithPlaceholder(editor, "Enter a tag")->setText("Aggro");
    buttonWithText(editor, "Add")->click();
    settle();

    expectWarningKeepsDialogOpen(editor, "This tag already exists.");
}

TEST_F(TagDialogs, TheDeckTagDialogWarnsAboutAnEmptyTag)
{
    const QWidget *dialog = openDeckTagDialog();
    buttonWithText(dialog, "Add Tag")->click();
    settle();

    expectWarningKeepsDialogOpen(dialog, "Tag name cannot be empty!");
}

TEST_F(TagDialogs, TheDeckTagDialogWarnsAboutADuplicateTag)
{
    const QWidget *dialog = openDeckTagDialog();
    lineEditWithPlaceholder(dialog, "Add a new tag")->setText("Combo");
    buttonWithText(dialog, "Add Tag")->click();
    settle();

    expectWarningKeepsDialogOpen(dialog, "This tag already exists.");
}

class TokenDialog : public DeckDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QPointer<DlgEditTokens> dialog;

    void SetUp() override
    {
        CardDatabaseManager::getInstance()->addCard(CardInfo::newInstance("Existing"));
        dialog = new DlgEditTokens(window.get());
        dialog->show();
    }

    QInputDialog *askForAToken() const
    {
        actionWithText(dialog, "Add token")->trigger();
        settle();
        return onlyOpen<QInputDialog>();
    }

    static QInputDialog *enter(QInputDialog *prompt, const QString &name)
    {
        prompt->setTextValue(name);
        prompt->accept();
        settle();
        return prompt;
    }
};

TEST_F(TokenDialog, ATakenNameIsRefusedAndAskedForAgainUntilCancelled)
{
    QInputDialog *prompt = askForAToken();
    ASSERT_NE(prompt, nullptr);
    enter(prompt, "Existing");
    clickInOnlyBox(QMessageBox::Ok, "The chosen name conflicts with an existing card or token.\nMake sure to enable "
                                    "the 'Token' set in the \"Manage sets\" dialog to display them correctly.");

    prompt = onlyOpen<QInputDialog>();
    ASSERT_NE(prompt, nullptr);
    prompt->reject();
    settle();

    EXPECT_TRUE(openDialogs<QInputDialog>().isEmpty());
    EXPECT_TRUE(openDialogs<QMessageBox>().isEmpty());
    EXPECT_TRUE(dialog->isVisible());
}

TEST_F(TokenDialog, AFreeNameAddsTheToken)
{
    QInputDialog *prompt = askForAToken();
    ASSERT_NE(prompt, nullptr);
    enter(prompt, "Goblin");

    const CardInfoPtr token = CardDatabaseManager::query()->getCardInfo("Goblin");
    ASSERT_TRUE(token);
    EXPECT_TRUE(token->getIsToken());
    EXPECT_TRUE(openDialogs<QInputDialog>().isEmpty());
}

TEST_F(TokenDialog, AColourlessTokenGetsAnEmptyColour)
{
    QInputDialog *prompt = askForAToken();
    ASSERT_NE(prompt, nullptr);
    enter(prompt, "Wurm");
    auto *tokens = dialog->findChild<QTreeView *>();
    auto *colour = dialog->findChild<QComboBox *>();
    ASSERT_NE(tokens, nullptr);
    ASSERT_NE(colour, nullptr);
    const QModelIndex first = tokens->model()->index(0, 0);
    tokens->setCurrentIndex(first);

    colour->setCurrentIndex(colour->findText("green"));
    colour->setCurrentIndex(colour->findText("colorless"));

    const CardInfoPtr token = CardDatabaseManager::query()->getCardInfo(first.data().toString());
    ASSERT_TRUE(token);
    EXPECT_EQ(token->getColors(), QString());
}

TEST_F(TokenDialog, ADialogDestroyedWithItsWindowStillSavesTheTokens)
{
    QInputDialog *prompt = askForAToken();
    ASSERT_NE(prompt, nullptr);
    enter(prompt, "Goblin");
    const QDir folder(SettingsCache::instance().getCustomCardDatabasePath());
    ASSERT_TRUE(folder.mkpath("."));
    const QString path = folder.filePath(QString(CardSet::TOKENS_SETNAME) + ".xml");
    QFile::remove(path);

    window.reset();

    QFile saved(path);
    ASSERT_TRUE(saved.open(QIODevice::ReadOnly));
    EXPECT_TRUE(saved.readAll().contains("Goblin"));
}

class DatabaseMenu : public DeckDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    CardDatabaseModel *source = new CardDatabaseModel(CardDatabaseManager::getInstance(), false, window.get());
    CardDatabaseDisplayModel *display = new CardDatabaseDisplayModel(window.get());
    CardDatabaseView *view = nullptr;
    QStringList heard;

    void SetUp() override
    {
        CardDatabaseManager::getInstance()->addCard(CardInfo::newInstance("Alpha"));
        CardDatabaseManager::getInstance()->addCard(CardInfo::newInstance("Beta"));
        display->setSourceModel(source);
        view = new CardDatabaseView(window.get(), display);
        QObject::connect(view, &CardDatabaseView::cardAdded, view,
                         [this](const QString &cardName, const QString &zoneName) {
                             heard.append(QStringLiteral("added %1 to %2").arg(cardName, zoneName));
                         });
        QObject::connect(view, &CardDatabaseView::cardChanged, view,
                         [this](const QString &cardName) { heard.append("shows " + cardName); });
        QObject::connect(view, &CardDatabaseView::selectPrintingClicked, view,
                         [this] { heard.append("select printing"); });
    }

    void TearDown() override
    {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *menu = qobject_cast<QMenu *>(widget); menu && menu->isVisible()) {
                menu->close();
            }
        }
        settle();
    }

    void select(const QString &cardName) const
    {
        const QModelIndexList found = display->match(display->index(0, CardDatabaseModel::NameColumn), Qt::DisplayRole,
                                                     cardName, 1, Qt::MatchExactly);
        ASSERT_EQ(found.size(), 1);
        view->setCurrentIndex(found.first());
    }

    QMenu *openMenuOn(const QString &cardName)
    {
        select(cardName);
        emit view->customContextMenuRequested(QPoint(1, 1));
        settle();
        heard.clear();
        return onlyOpenTopLevel<QMenu>();
    }
};

TEST_F(DatabaseMenu, AddToDeckAddsTheCardTheMenuWasOpenedOn)
{
    const QMenu *menu = openMenuOn("Alpha");
    ASSERT_NE(menu, nullptr);
    select("Beta");
    heard.clear();

    actionWithText(menu, "Add to Deck")->trigger();
    EXPECT_EQ(heard, QStringList{QStringLiteral("added Alpha to %1").arg(DECK_ZONE_MAIN)});
}

TEST_F(DatabaseMenu, SelectPrintingShowsTheCardTheMenuWasOpenedOnFirst)
{
    const QMenu *menu = openMenuOn("Alpha");
    ASSERT_NE(menu, nullptr);
    select("Beta");
    heard.clear();

    actionWithText(menu, "Select Printing")->trigger();
    EXPECT_EQ(heard, (QStringList{"shows Alpha", "select printing"}));
}

class CodConversion : public DeckDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QTemporaryDir folder;
    int conversions = 0;
    std::optional<bool> canStoreTags;

    void SetUp() override
    {
        SettingsCache::instance().visualDeckStorage().setVisualDeckStoragePromptForConversion(true);
        SettingsCache::instance().visualDeckStorage().setVisualDeckStorageAlwaysConvert(false);
    }

    void prompt(const QString &fileName)
    {
        DialogConvertDeckToCodFormat::promptIfRequired(
            window.get(), folder.filePath(fileName),
            [this] {
                ++conversions;
                return true;
            },
            [this](bool convertible) {
                EXPECT_FALSE(canStoreTags.has_value()) << "answered twice";
                canStoreTags = convertible;
            });
        settle();
    }

    void answerConversion(bool yes) const
    {
        auto *dialog = onlyOpen<DialogConvertDeckToCodFormat>();
        ASSERT_NE(dialog, nullptr);
        yes ? dialog->accept() : dialog->reject();
        settle();
    }

    void createCodVersion() const
    {
        QFile file(folder.filePath("deck.cod"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    }
};

TEST_F(CodConversion, ACodDeckNeedsNoPrompt)
{
    prompt("deck.cod");

    EXPECT_EQ(canStoreTags, true);
    EXPECT_EQ(conversions, 0);
    EXPECT_TRUE(openDialogs<QDialog>().isEmpty());
}

TEST_F(CodConversion, YesConvertsTheDeck)
{
    prompt("deck.txt");
    EXPECT_FALSE(canStoreTags.has_value());
    answerConversion(true);

    EXPECT_EQ(conversions, 1);
    EXPECT_EQ(canStoreTags, true);
}

TEST_F(CodConversion, NoLeavesTheDeckAndKeepsAsking)
{
    prompt("deck.txt");
    answerConversion(false);

    EXPECT_EQ(conversions, 0);
    EXPECT_EQ(canStoreTags, false);
    EXPECT_TRUE(SettingsCache::instance().visualDeckStorage().getVisualDeckStoragePromptForConversion());
}

TEST_F(CodConversion, AnExistingCodFileIsOverwrittenOnlyAfterYes)
{
    createCodVersion();
    prompt("deck.txt");
    answerConversion(true);
    EXPECT_EQ(conversions, 0);
    EXPECT_FALSE(canStoreTags.has_value());

    clickInOnlyBox(QMessageBox::Yes, "A .cod version of this deck already exists. Overwrite it?");
    EXPECT_EQ(conversions, 1);
    EXPECT_EQ(canStoreTags, true);
}

TEST_F(CodConversion, NoToOverwritingKeepsTheExistingCodFile)
{
    createCodVersion();
    prompt("deck.txt");
    answerConversion(true);

    clickInOnlyBox(QMessageBox::No);
    EXPECT_EQ(conversions, 0);
    EXPECT_EQ(canStoreTags, false);
}

class PrintingOverride : public DeckDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    std::optional<bool> accepted;

    void SetUp() override
    {
        SettingsCache::instance().cardsDisplay().setOverrideAllCardArtWithPersonalPreference(false);
        pictureCacheClears() = {};
    }

    void ask(bool enable)
    {
        OverridePrintingWarning::confirm(window.get(), enable, [this](bool answer) { accepted = answer; });
        settle();
    }
};

TEST_F(PrintingOverride, YesChangesTheSetting)
{
    ask(true);
    EXPECT_FALSE(accepted.has_value());
    clickInOnlyBox(QMessageBox::Yes);

    EXPECT_EQ(accepted, true);
    EXPECT_TRUE(SettingsCache::instance().cardsDisplay().getOverrideAllCardArtWithPersonalPreference());
    EXPECT_EQ(pictureCacheClears().pixmap, 1);
    EXPECT_EQ(pictureCacheClears().network, 1);
}

TEST_F(PrintingOverride, NoKeepsTheSetting)
{
    ask(true);
    clickInOnlyBox(QMessageBox::No);

    EXPECT_EQ(accepted, false);
    EXPECT_FALSE(SettingsCache::instance().cardsDisplay().getOverrideAllCardArtWithPersonalPreference());
    EXPECT_EQ(pictureCacheClears().pixmap, 0);
    EXPECT_EQ(pictureCacheClears().network, 0);
}

class BracketPrompt : public DeckDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QPointer<CommanderBracketWidget> first = new CommanderBracketWidget(window.get());
    QPointer<CommanderBracketWidget> second = new CommanderBracketWidget(window.get());

    void SetUp() override
    {
        SettingsCache::instance().deckEditor().setCommanderSpellbookIntegrationEnabled(
            commanderSpellbookIntegrationEnabledIndexUnprompted);
    }

    static QSharedPointer<DeckList> commanderDeck()
    {
        auto deck = QSharedPointer<DeckList>::create();
        deck->setGameFormat("commander");
        return deck;
    }

    void loadCommanderDecks() const
    {
        first->setDeck(commanderDeck());
        second->setDeck(commanderDeck());
        settle();
    }

    // A closed prompt is deleted after its answer, and the next deck editor asks once it is gone.
    static void settleTwice()
    {
        settle();
        settle();
    }
};

TEST_F(BracketPrompt, OneQuestionServesEveryDeckEditor)
{
    loadCommanderDecks();
    QDialog *prompt = onlyOpen<QDialog>();
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->parent(), first);

    buttonWithText(prompt, "Enable")->click();
    settleTwice();

    EXPECT_TRUE(openDialogs<QDialog>().isEmpty());
    EXPECT_FALSE(first->isHidden());
    EXPECT_FALSE(second->isHidden());
}

TEST_F(BracketPrompt, AClosedQuestionIsAskedAgainForTheNextDeckEditor)
{
    loadCommanderDecks();
    QDialog *prompt = onlyOpen<QDialog>();
    ASSERT_NE(prompt, nullptr);
    prompt->reject();
    settleTwice();
    EXPECT_TRUE(first->isHidden());

    prompt = onlyOpen<QDialog>();
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->parent(), second);
    buttonWithText(prompt, "Disable")->click();
    settleTwice();

    EXPECT_TRUE(openDialogs<QDialog>().isEmpty());
    EXPECT_TRUE(second->isHidden());
}

TEST_F(BracketPrompt, ADeckEditorClosedWhileAskingHandsTheQuestionOn)
{
    loadCommanderDecks();
    ASSERT_EQ(openDialogs<QDialog>().size(), 1);

    first->deleteLater();
    settleTwice();

    const QDialog *prompt = onlyOpen<QDialog>();
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->parent(), second);
}

// As in a browser tab, which reaches only its own storage.
class DecksInStorage : public DeckDialogs // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QDir decks{SettingsCache::instance().paths().getDeckPath()};
    int answers = 0;
    QString answered;

    void SetUp() override
    {
        SettingsCache::instance().setDeviceFoldersReachableForTesting(false);
        ASSERT_TRUE(decks.removeRecursively());
        ASSERT_TRUE(decks.mkpath("."));
    }

    void TearDown() override
    {
        SettingsCache::instance().setDeviceFoldersReachableForTesting(true);
    }

    void record(const QString &fileName)
    {
        ++answers;
        answered = fileName;
    }

    void askToSave(const QString &deckName)
    {
        DeckSavePrompt::chooseFile(window.get(), deckName, [this](const QString &fileName) { record(fileName); });
        settle();
    }

    void chooseDeck()
    {
        DlgLoadDeck::choose(window.get(), [this](const QString &fileName) { record(fileName); });
        settle();
    }

    void answerName(const QString &name) const
    {
        auto *prompt = onlyOpen<QInputDialog>();
        ASSERT_NE(prompt, nullptr);
        prompt->setTextValue(name);
        prompt->accept();
        settle();
    }

    void addFile(const QString &relativePath) const
    {
        ASSERT_TRUE(decks.mkpath(QFileInfo(relativePath).path()));
        QFile file(decks.filePath(relativePath));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    }

    void addDeck(const QString &relativePath, const QString &name) const
    {
        ASSERT_TRUE(decks.mkpath(QFileInfo(relativePath).path()));
        DeckList deck;
        deck.setName(name);
        ASSERT_TRUE(DeckLoader::saveToFile(deck, decks.filePath(relativePath), DeckFileFormat::Cockatrice));
    }

    QListWidget *deckList() const
    {
        const auto *chooser = onlyOpen<QDialog>();
        return chooser ? chooser->findChild<QListWidget *>() : nullptr;
    }

    static QStringList shownDecks(const QListWidget *list)
    {
        QStringList shown;
        for (int row = 0; row < list->count(); ++row) {
            if (!list->item(row)->isHidden()) {
                shown.append(list->item(row)->text());
            }
        }
        return shown;
    }

    QPushButton *okButton() const
    {
        const auto *chooser = onlyOpen<QDialog>();
        return chooser ? chooser->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok) : nullptr;
    }
};

TEST_F(DecksInStorage, SaveAsAsksForANameAndTheDeckIsWrittenIntoTheDecksFolder)
{
    askToSave("Burn");
    EXPECT_TRUE(openDialogs<QFileDialog>().isEmpty());
    const auto *prompt = onlyOpen<QInputDialog>();
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->textValue(), "Burn");

    answerName("Burn");
    EXPECT_EQ(answers, 1);
    EXPECT_EQ(answered, decks.filePath("Burn.cod"));

    DeckList deck;
    deck.setName("Burn");
    ASSERT_TRUE(DeckLoader::saveToFile(deck, answered, DeckFileFormat::getFormatFromName(answered)));
    EXPECT_EQ(decks.entryList(QDir::Files), QStringList{"Burn.cod"});
}

TEST_F(DecksInStorage, ANamelessDeckIsOfferedADefaultName)
{
    askToSave("");
    const auto *prompt = onlyOpen<QInputDialog>();
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->textValue(), "Untitled deck");
}

TEST_F(DecksInStorage, UnsafeNamesBecomeSafeFileNames)
{
    const QList<std::pair<QString, QString>> cases = {
        {"  Burn  ", "Burn.cod"},
        {"../Burn", "Burn.cod"},
        {"a/b\\c:d*e?f\"g<h>i|j", "abcdefghij.cod"},
        {".hidden", "hidden.cod"},
        {QStringLiteral("Bu\u0001rn"), "Burn.cod"},
        {"Burn.", "Burn.cod"},
        {"Burn v1.2", "Burn v1.2.cod"},
        {"Burn.txt", "Burn.txt"},
        {"Burn.DEK", "Burn.DEK"},
        {"", "Untitled deck.cod"},
        {" / ", "Untitled deck.cod"},
        {QStringLiteral("\u00A0\u00A0"), "Untitled deck.cod"},
        {QStringLiteral("\u3000Burn\u3000"), "Burn.cod"},
        {QStringLiteral("\u2028"), "Untitled deck.cod"},
    };
    for (const auto &[typed, expected] : cases) {
        answers = 0;
        askToSave("Burn");
        answerName(typed);
        EXPECT_EQ(answers, 1) << typed.toStdString();
        EXPECT_EQ(answered, decks.filePath(expected)) << typed.toStdString();
    }
}

TEST_F(DecksInStorage, ALongNameIsCutToTheLengthAFileSystemTakes)
{
    const QString face = QString::fromUtf8("\xF0\x9F\x98\x80");
    const QList<std::pair<QString, QString>> cases = {
        {QString(300, 'B'), QString(251, 'B') + ".cod"},
        {QString(300, 'B') + ".txt", QString(251, 'B') + ".txt"},
        {face.repeated(100), face.repeated(62) + ".cod"},
        {QString(250, 'B') + " B", QString(250, 'B') + ".cod"},
    };
    for (const auto &[typed, expected] : cases) {
        answers = 0;
        askToSave("Burn");
        answerName(typed);
        EXPECT_EQ(answers, 1) << typed.toStdString();
        EXPECT_EQ(answered, decks.filePath(expected)) << typed.toStdString();
        EXPECT_LE(QFileInfo(answered).fileName().toUtf8().size(), 255) << typed.toStdString();
    }
}

TEST_F(DecksInStorage, AnExistingFileIsReplacedOnlyAfterYes)
{
    addFile("Burn.cod");
    askToSave("Burn");
    answerName("Burn");
    EXPECT_EQ(answers, 0);

    const auto *box = onlyOpen<QMessageBox>();
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->defaultButton(), box->button(QMessageBox::No));
    clickInOnlyBox(QMessageBox::Yes, "A deck file named \"Burn.cod\" already exists. Replace it?");
    EXPECT_EQ(answers, 1);
    EXPECT_EQ(answered, decks.filePath("Burn.cod"));
}

TEST_F(DecksInStorage, NoToReplacingAsksForTheNameAgain)
{
    addFile("Burn.cod");
    askToSave("Burn");
    answerName("Burn");
    clickInOnlyBox(QMessageBox::No);
    EXPECT_EQ(answers, 0);

    const auto *prompt = onlyOpen<QInputDialog>();
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->textValue(), "Burn");
    answerName("Burn 2");
    EXPECT_EQ(answers, 1);
    EXPECT_EQ(answered, decks.filePath("Burn 2.cod"));
}

TEST_F(DecksInStorage, CancellingTheNameReportsNoFileOnce)
{
    askToSave("Burn");
    auto *prompt = onlyOpen<QInputDialog>();
    ASSERT_NE(prompt, nullptr);
    prompt->reject();
    settle();

    EXPECT_EQ(answers, 1);
    EXPECT_TRUE(answered.isEmpty());
    EXPECT_TRUE(openDialogs<QDialog>().isEmpty());
}

TEST_F(DecksInStorage, CancellingAfterRefusingToReplaceReportsNoFileOnce)
{
    addFile("Burn.cod");
    askToSave("Burn");
    answerName("Burn");
    clickInOnlyBox(QMessageBox::No);
    auto *prompt = onlyOpen<QInputDialog>();
    ASSERT_NE(prompt, nullptr);
    prompt->reject();
    settle();

    EXPECT_EQ(answers, 1);
    EXPECT_TRUE(answered.isEmpty());
}

TEST_F(DecksInStorage, WhereDeviceFoldersAreReachableAFileDialogChoosesWhereToSave)
{
    SettingsCache::instance().setDeviceFoldersReachableForTesting(true);

    askToSave("Burn");
    EXPECT_TRUE(openDialogs<QInputDialog>().isEmpty());
    QFileDialog *save = onlyOpen<QFileDialog>();
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(save->acceptMode(), QFileDialog::AcceptSave);
    EXPECT_EQ(QFileInfo(save->directory().path()).canonicalFilePath(), QFileInfo(decks.path()).canonicalFilePath());
    EXPECT_EQ(save->defaultSuffix(), "cod");
    EXPECT_EQ(save->nameFilters(), DeckLoader::FILE_NAME_FILTERS);
    const auto *nameField = save->findChild<QLineEdit *>("fileNameEdit");
    ASSERT_NE(nameField, nullptr);
    EXPECT_EQ(nameField->text(), "Burn");
    DialogTest::answerFileDialog(save, decks.filePath("Burn.cod"));
    EXPECT_EQ(answers, 1);
    EXPECT_EQ(QFileInfo(answered).fileName(), "Burn.cod");

    askToSave("Burn");
    DialogTest::answerFileDialog(onlyOpen<QFileDialog>(), {});
    EXPECT_EQ(answers, 2);
    EXPECT_TRUE(answered.isEmpty());
}

TEST_F(DecksInStorage, LoadListsTheDeckFilesOfTheFolderByRelativePath)
{
    addFile("b.cod");
    addFile("A.txt");
    addFile("e.dec");
    addFile("notes.pdf");
    addFile("sub/c.dek");
    addFile("sub/deep/d.mwDeck");

    chooseDeck();
    EXPECT_TRUE(openDialogs<QFileDialog>().isEmpty());
    const QListWidget *list = deckList();
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(shownDecks(list), (QStringList{"A.txt", "b.cod", "e.dec", "sub/c.dek", "sub/deep/d.mwDeck"}));
}

TEST_F(DecksInStorage, DoubleClickLoadsTheDeck)
{
    addDeck("red/Burn.cod", "Burn");
    addDeck("Zoo.cod", "Zoo");
    chooseDeck();
    QListWidget *list = deckList();
    ASSERT_NE(list, nullptr);
    const QListWidgetItem *item = list->findItems("red/Burn.cod", Qt::MatchExactly).value(0);
    ASSERT_NE(item, nullptr);

    const QPoint at = list->visualItemRect(item).center();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, at);
    // QTest's own double click does not reach an item view's double click handler.
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, at, list->viewport()->mapToGlobal(at), Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &doubleClick);
    settle();
    EXPECT_EQ(answers, 1);
    EXPECT_EQ(answered, decks.filePath("red/Burn.cod"));
    EXPECT_TRUE(openDialogs<QDialog>().isEmpty());

    const auto loaded = DeckLoader::loadFromFile(answered, DeckFileFormat::getFormatFromName(answered));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->deckList.getName(), "Burn");
}

TEST_F(DecksInStorage, OkLoadsTheSelectedDeck)
{
    addDeck("Burn.cod", "Burn");
    addDeck("Zoo.cod", "Zoo");
    chooseDeck();
    QListWidget *list = deckList();
    ASSERT_NE(list, nullptr);
    list->setCurrentItem(list->findItems("Zoo.cod", Qt::MatchExactly).value(0));
    okButton()->click();
    settle();

    EXPECT_EQ(answers, 1);
    EXPECT_EQ(answered, decks.filePath("Zoo.cod"));
}

TEST_F(DecksInStorage, TheFilterShowsOnlyMatchingDecks)
{
    addFile("Burn.cod");
    addFile("red/Sligh.cod");
    addFile("Zoo.cod");
    chooseDeck();
    QListWidget *list = deckList();
    ASSERT_NE(list, nullptr);
    auto *filter = list->parentWidget()->findChild<QLineEdit *>();
    ASSERT_NE(filter, nullptr);

    QTest::keyClicks(filter, "RED");
    EXPECT_EQ(shownDecks(list), QStringList{"red/Sligh.cod"});
    ASSERT_NE(list->currentItem(), nullptr);
    EXPECT_EQ(list->currentItem()->text(), "red/Sligh.cod");
    EXPECT_TRUE(okButton()->isEnabled());

    filter->setText("Nothing");
    EXPECT_TRUE(shownDecks(list).isEmpty());
    EXPECT_FALSE(okButton()->isEnabled());

    filter->clear();
    EXPECT_EQ(shownDecks(list).size(), 3);
    EXPECT_TRUE(okButton()->isEnabled());
}

TEST_F(DecksInStorage, CancelLoadsNothing)
{
    addFile("Burn.cod");
    chooseDeck();
    auto *chooser = onlyOpen<QDialog>();
    ASSERT_NE(chooser, nullptr);
    chooser->reject();
    settle();

    EXPECT_EQ(answers, 0);
    EXPECT_TRUE(openDialogs<QDialog>().isEmpty());
}

TEST_F(DecksInStorage, AnEmptyFolderSaysHowToGetADeckIn)
{
    addFile("notes.pdf");
    chooseDeck();

    const auto *box = onlyOpen<QMessageBox>();
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->text().startsWith("No deck is saved in this browser yet."));
    clickInOnlyBox(QMessageBox::Ok);
    EXPECT_EQ(answers, 0);
}

TEST_F(DecksInStorage, WhereDeviceFoldersAreReachableAFileDialogChoosesTheDeckToLoad)
{
    SettingsCache::instance().setDeviceFoldersReachableForTesting(true);
    addFile("Zoo.cod");

    chooseDeck();
    QFileDialog *load = onlyOpen<DlgLoadDeck>();
    ASSERT_NE(load, nullptr);
    DialogTest::answerFileDialog(load, decks.filePath("Zoo.cod"));
    EXPECT_EQ(answers, 1);
    EXPECT_EQ(QFileInfo(answered).fileName(), "Zoo.cod");
}

class DecksOnTheDevice : public DecksInStorage // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    DialogTest::DevicePicker picker;

    static DeckList burn()
    {
        DeckList deck;
        deck.setName("Burn");
        deck.addCards("Lightning Bolt", 4, DECK_ZONE_MAIN);
        return deck;
    }

    void import()
    {
        DeckSavePrompt::importFromDevice(window.get(), [this](const QString &path) { record(path); });
        settle();
        EXPECT_EQ(picker.filter, DeckLoader::FILE_NAME_FILTERS.join(";;"));
        EXPECT_TRUE(openDialogs<QFileDialog>().isEmpty());
    }

    QByteArray stored(const QString &relativePath) const
    {
        QFile file(decks.filePath(relativePath));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
};

TEST_F(DecksOnTheDevice, ImportCopiesThePickedDeckIntoTheDecksFolder)
{
    const QByteArray bytes = DeckLoader::toBytes(burn(), DeckFileFormat::Cockatrice);
    import();
    picker.pick("Burn.cod", bytes);

    EXPECT_EQ(answers, 1);
    EXPECT_EQ(answered, decks.filePath("Burn.cod"));
    EXPECT_EQ(stored("Burn.cod"), bytes);
    EXPECT_TRUE(openDialogs<QDialog>().isEmpty());
}

TEST_F(DecksOnTheDevice, AnImportedFileGetsASafeNameMadeFromItsOwn)
{
    const QByteArray plain = DeckLoader::toBytes(burn(), DeckFileFormat::PlainText);
    import();
    picker.pick("/device/folder/Red: Burn?.dek", plain);
    EXPECT_EQ(answered, decks.filePath("Red Burn.dek"));
    EXPECT_EQ(stored("Red Burn.dek"), plain);

    import();
    picker.pick("Burn list", plain);
    EXPECT_EQ(answered, decks.filePath("Burn list.cod"));
    EXPECT_EQ(answers, 2);
}

TEST_F(DecksOnTheDevice, ImportReplacesAFileOnlyAfterYes)
{
    addFile("Burn.cod");
    const QByteArray bytes = DeckLoader::toBytes(burn(), DeckFileFormat::Cockatrice);

    import();
    picker.pick("Burn.cod", bytes);
    const auto *box = onlyOpen<QMessageBox>();
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->defaultButton(), box->button(QMessageBox::No));
    clickInOnlyBox(QMessageBox::No, "A file named \"Burn.cod\" already exists. Replace it?");
    EXPECT_EQ(answers, 0);
    EXPECT_TRUE(stored("Burn.cod").isEmpty());

    import();
    picker.pick("Burn.cod", bytes);
    clickInOnlyBox(QMessageBox::Yes);
    EXPECT_EQ(answers, 1);
    EXPECT_EQ(stored("Burn.cod"), bytes);
}

TEST_F(DecksOnTheDevice, ImportingAFileThatHoldsNoDeckSaysSoAndWritesNothing)
{
    import();
    picker.pick("notes.cod", "%%% ###\n");

    clickInOnlyBox(QMessageBox::Ok, "Could not open deck at notes.cod");
    EXPECT_EQ(answers, 0);
    EXPECT_TRUE(decks.entryList(QDir::Files).isEmpty());
}

TEST_F(DecksOnTheDevice, ACancelledImportDoesNothing)
{
    import();
    picker.pick({}, {});

    EXPECT_EQ(answers, 0);
    EXPECT_TRUE(openDialogs<QDialog>().isEmpty());
    EXPECT_TRUE(decks.entryList(QDir::Files).isEmpty());
}

TEST_F(DecksOnTheDevice, ExportSendsTheDeckInTheFormatOfItsFileName)
{
    DeckSavePrompt::exportToDevice(burn(), decks.filePath("red/Burn list.txt"));
    DeckList zoo = burn();
    zoo.setName("Zoo: Naya");
    DeckSavePrompt::exportToDevice(zoo, {});
    DeckSavePrompt::exportToDevice(DeckList(), {});

    const QList<std::pair<QString, QByteArray>> expected = {
        {"Burn list.txt", DeckLoader::toBytes(burn(), DeckFileFormat::PlainText)},
        {"Zoo Naya.cod", DeckLoader::toBytes(zoo, DeckFileFormat::Cockatrice)},
        {"Untitled deck.cod", DeckLoader::toBytes(DeckList(), DeckFileFormat::Cockatrice)},
    };
    EXPECT_EQ(picker.saved, expected);
}

TEST_F(DecksOnTheDevice, ExportingAStoredFileSendsItsBytesAsTheyAre)
{
    addDeck("red/Burn.cod", "Burn");

    DeviceFiles::exportFile(window.get(), decks.filePath("red/Burn.cod"));

    ASSERT_EQ(picker.saved.size(), 1);
    EXPECT_EQ(picker.saved.at(0).first, "Burn.cod");
    EXPECT_EQ(picker.saved.at(0).second, stored("red/Burn.cod"));
}

class AnalyticsLayout : public ::testing::Test // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    static QString inLayoutsFile()
    {
        const QSettings file(SettingsCache::instance().getSettingsPath() + "layouts.ini", QSettings::IniFormat);
        return file.value("deckAnalytics/layout").toString();
    }

    void SetUp() override
    {
        QSettings().remove("deckAnalytics");
        QSettings(SettingsCache::instance().getSettingsPath() + "layouts.ini", QSettings::IniFormat)
            .remove("deckAnalytics");
    }

    void TearDown() override
    {
        QSettings().remove("deckAnalytics");
    }
};

TEST_F(AnalyticsLayout, ASavedLayoutGoesToAFileInTheSettingsFolder)
{
    SettingsCache::instance().layouts().setDeckAnalyticsLayout("[1]");

    EXPECT_EQ(inLayoutsFile(), "[1]");
    EXPECT_FALSE(QSettings().contains("deckAnalytics/layout"));
    EXPECT_EQ(SettingsCache::instance().layouts().getDeckAnalyticsLayout(), "[1]");
}

TEST_F(AnalyticsLayout, ALayoutOnlyInTheNativeStoreIsCopiedOnceThenReadFromTheFile)
{
    QSettings().setValue("deckAnalytics/layout", "[old]");

    EXPECT_EQ(SettingsCache::instance().layouts().getDeckAnalyticsLayout(), "[old]");
    EXPECT_EQ(inLayoutsFile(), "[old]");

    QSettings().setValue("deckAnalytics/layout", "[changed]");
    EXPECT_EQ(SettingsCache::instance().layouts().getDeckAnalyticsLayout(), "[old]");
}

TEST_F(AnalyticsLayout, WithoutAnyLayoutNothingIsWritten)
{
    EXPECT_TRUE(SettingsCache::instance().layouts().getDeckAnalyticsLayout().isEmpty());
    const QSettings file(SettingsCache::instance().getSettingsPath() + "layouts.ini", QSettings::IniFormat);
    EXPECT_FALSE(file.contains("deckAnalytics/layout"));
}
} // namespace

int main(int argc, char **argv)
{
    return DialogTest::run(argc, argv, QStringLiteral("deck-dialogs-test"), [] {
        ThemeManager theme;
        themeManager = &theme;
        CardDatabaseManager::setCardDatabasePathProvider(&SettingsCache::instance());

        const int result = RUN_ALL_TESTS();

        QDir(SettingsCache::instance().getDataPath()).removeRecursively();
        QDir(SettingsCache::instance().getCachePath()).removeRecursively();
        return result;
    });
}

#include "client/settings/cache_settings.h"
#include "dialog_test_support.h"
#include "interface/card_picture_loader/card_picture_loader.h"
#include "interface/theme_manager.h"
#include "interface/widgets/deck_editor/card_database_view.h"
#include "interface/widgets/deck_editor/deck_state_manager.h"
#include "interface/widgets/deck_editor/deck_zone_dialog.h"
#include "interface/widgets/dialogs/dlg_convert_deck_to_cod_format.h"
#include "interface/widgets/dialogs/dlg_default_tags_editor.h"
#include "interface/widgets/dialogs/dlg_edit_tokens.h"
#include "interface/widgets/dialogs/dlg_load_deck_from_website.h"
#include "interface/widgets/dialogs/override_printing_warning.h"
#include "interface/widgets/utility/non_blocking_dialogs.h"
#include "interface/widgets/visual_deck_storage/deck_preview/deck_preview_tag_dialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstring>
#include <gtest/gtest.h>
#include <libcockatrice/card/card_info.h>
#include <libcockatrice/card/database/card_database.h>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/card/database/card_database_querier.h>
#include <libcockatrice/deck_list/tree/inner_deck_list_node.h>
#include <libcockatrice/models/database/card_database_display_model.h>
#include <libcockatrice/models/database/card_database_model.h>
#include <libcockatrice/models/deck_list/deck_list_model.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <libcockatrice/settings/cards_display_settings.h>
#include <libcockatrice/settings/visual_deck_storage_settings.h>
#include <optional>

ThemeManager *themeManager = nullptr; // NOSONAR: the client declares it extern and assigns it
RNG_Abstract *rng = nullptr;          // NOSONAR: the client declares it extern and assigns it

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
        open(QIODevice::ReadOnly);
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

    static void answer(DeckZoneDialog *dialog, const QString &name)
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

} // namespace

int main(int argc, char **argv)
{
    return DialogTest::run(argc, argv, QStringLiteral("deck-dialogs-test"), [] {
        ThemeManager theme;
        themeManager = &theme;

        const int result = RUN_ALL_TESTS();

        QDir(SettingsCache::instance().getDataPath()).removeRecursively();
        QDir(SettingsCache::instance().getCachePath()).removeRecursively();
        return result;
    });
}

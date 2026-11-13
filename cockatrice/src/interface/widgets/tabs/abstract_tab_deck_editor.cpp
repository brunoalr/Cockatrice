/**
 * @file abstract_tab_deck_editor.cpp
 * @brief Implementation of the AbstractTabDeckEditor class.
 *
 * Handles deck editor tab UI, deck management, card operations, clipboard
 * operations, printing, deck export, remote uploads, and interactions with
 * external services (DeckStats, TappedOut, etc.).
 */

#include "abstract_tab_deck_editor.h"

#include "../../../client/settings/cache_settings.h"
#include "../../../client/settings/shortcuts_settings.h"
#include "../../deck_loader/deck_loader.h"
#include "../cards/additional_info/deck_color_identity.h"
#include "../cards/card_info_frame_widget.h"
#include "../client/network/interfaces/deck_stats_interface.h"
#include "../client/network/interfaces/tapped_out_interface.h"
#include "../deck_editor/deck_editor_card_database_dock_widget.h"
#include "../deck_editor/deck_editor_card_info_dock_widget.h"
#include "../deck_editor/deck_editor_deck_dock_widget.h"
#include "../deck_editor/deck_editor_filter_dock_widget.h"
#include "../deck_editor/deck_editor_printing_selector_dock_widget.h"
#include "../deck_editor/deck_state_manager.h"
#include "../interface/card_picture_loader/card_picture_loader.h"
#include "../interface/widgets/dialogs/dlg_load_deck.h"
#include "../interface/widgets/dialogs/dlg_load_deck_from_clipboard.h"
#include "../interface/widgets/dialogs/dlg_load_deck_from_website.h"
#include "../interface/widgets/dialogs/dlg_share_deck.h"
#include "../menus/deck_editor_menu.h"
#include "../printing_selector/printing_selector.h"
#include "../utility/non_blocking_dialogs.h"
#include "../utility/visibility_change_listener.h"
#include "libcockatrice/card/database/card_database_querier.h"
#include "libcockatrice/card/printing/exact_card.h"
#include "libcockatrice/deck_list/deck_list.h"
#include "libcockatrice/utility/card_ref.h"
#include "tab_supervisor.h"

#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileDialog>
#include <QFlags>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QSharedPointer>
#include <QUrl>
#include <google/protobuf/stubs/port.h>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/deck_list/deck_file_format.h>
#include <libcockatrice/deck_list/loaded_deck.h>
#include <libcockatrice/models/database/card_database_model.h>
#include <libcockatrice/network/client/abstract/abstract_client.h>
#include <libcockatrice/protocol/pb/command_deck_upload.pb.h>
#include <libcockatrice/protocol/pb/response.pb.h>
#include <libcockatrice/protocol/pending_command.h>
#include <libcockatrice/settings/deck_editor_settings.h>
#include <libcockatrice/settings/paths_settings.h>
#include <libcockatrice/settings/recents_settings.h>
#include <libcockatrice/utility/string_limits.h>
#include <optional>
#include <string>

namespace
{
void reportSaved(const std::function<void(bool saved)> &then, bool saved)
{
    if (then) {
        then(saved);
    }
}
} // namespace

/**
 * @brief Constructs the AbstractTabDeckEditor.
 * Initializes all dock widgets and connects signals/slots.
 * @param _tabSupervisor The tab supervisor managing this tab.
 */
AbstractTabDeckEditor::AbstractTabDeckEditor(TabSupervisor *_tabSupervisor) : Tab(_tabSupervisor)
{
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);

    deckStateManager = new DeckStateManager(this);
    unsavedDeckGuard = std::make_unique<UnsavedDeckGuard>(
        this, deckStateManager, [this] { tabSupervisor->setCurrentWidget(this); },
        [this](std::function<void(bool saved)> then) { actSaveDeck(std::move(then)); });

    databaseModel = new CardDatabaseModel(CardDatabaseManager::getInstance(), true, this);
    databaseModel->setObjectName("databaseModel");

    cardDatabaseDockWidget = new DeckEditorCardDatabaseDockWidget(this);
    deckDockWidget = new DeckEditorDeckDockWidget(this);
    cardInfoDockWidget = new DeckEditorCardInfoDockWidget(this);
    filterDockWidget = new DeckEditorFilterDockWidget(this);
    printingSelectorDockWidget = new DeckEditorPrintingSelectorDockWidget(this);

    // Connect deck signals to this tab
    connect(deckStateManager, &DeckStateManager::isModifiedChanged, this, &AbstractTabDeckEditor::onDeckModified);
    connect(deckDockWidget, &DeckEditorDeckDockWidget::selectedCardChanged, this, &AbstractTabDeckEditor::updateCard);

    // Connect filter signals
    connect(filterDockWidget, &DeckEditorFilterDockWidget::clearAllDatabaseFilters, cardDatabaseDockWidget,
            &DeckEditorCardDatabaseDockWidget::clearAllDatabaseFilters);

    // Connect shortcut changes
    connect(&SettingsCache::instance().shortcuts(), &ShortcutsSettings::shortCutChanged, this,
            &AbstractTabDeckEditor::refreshShortcuts);
}

void AbstractTabDeckEditor::registerDockWidget(QMenu *_viewMenu, QDockWidget *widget, const QSize &defaultSize)
{
    QMenu *menu = _viewMenu->addMenu(QString());

    QAction *aVisible = menu->addAction(QString());
    aVisible->setCheckable(true);

    QAction *aFloating = menu->addAction(QString());
    aFloating->setCheckable(true);
    aFloating->setEnabled(false);

    // user interaction
    connect(aVisible, &QAction::triggered, widget, [widget](bool checked) { widget->setVisible(checked); });
    connect(aFloating, &QAction::triggered, this, [widget](bool checked) { widget->setFloating(checked); });

    // sync aFloating's enabled state with aVisible's checked state
    connect(aVisible, &QAction::toggled, aFloating, [aFloating](bool checked) { aFloating->setEnabled(checked); });

    // sync aFloating with dockWidget's floating state
    connect(widget, &QDockWidget::topLevelChanged, aFloating,
            [aFloating](bool topLevel) { aFloating->setChecked(topLevel); });

    // sync aVisible with dockWidget's visible state
    auto filter = new VisibilityChangeListener(widget);
    connect(filter, &VisibilityChangeListener::visibilityChanged, aVisible,
            [aVisible](bool visible) { aVisible->setChecked(visible); });

    dockToActions.insert(widget, {menu, aVisible, aFloating, defaultSize});
}

void AbstractTabDeckEditor::updateCard(const ExactCard &card)
{
    cardInfoDockWidget->updateCard(card);
    printingSelectorDockWidget->printingSelector->setCard(card.getCardPtr());
}

void AbstractTabDeckEditor::updateCardInfo(const ExactCard &card)
{
    cardInfoDockWidget->updateCard(card);
}

/** @brief Placeholder: called when the deck changes. */
void AbstractTabDeckEditor::onDeckChanged()
{
}

/**
 * @brief Marks the tab as modified and updates the save menu status.
 */
void AbstractTabDeckEditor::onDeckModified()
{
    deckMenu->setSaveStatus(!deckStateManager->isBlankNewDeck());
    emit tabTextChanged(this, getTabText());
}

void AbstractTabDeckEditor::addCard(const ExactCard &card, const QString &zoneName)
{
    deckStateManager->addCard(card, zoneName);
}

void AbstractTabDeckEditor::decrementCard(const ExactCard &card, const QString &zoneName)
{
    deckStateManager->decrementCard(card, zoneName);
}

/**
 * @brief Opens a deck in this tab.
 * @param deck The deck
 */
void AbstractTabDeckEditor::openDeck(const LoadedDeck &deck)
{
    setDeck(deck);

    if (!deck.lastLoadInfo.fileName.isEmpty()) {
        SettingsCache::instance().recents().updateRecentlyOpenedDeckPaths(deck.lastLoadInfo.fileName);
    }
}

/**
 * @brief Sets the currently active deck.
 * @param _deck The deck
 */
void AbstractTabDeckEditor::setDeck(const LoadedDeck &_deck)
{
    deckStateManager->replaceDeck(_deck);
    CardPictureLoader::cacheCardPixmaps(CardDatabaseManager::query()->getCards(_deck.deckList.getCardRefList()));

    dockToActions.value(deckDockWidget).aVisible->setChecked(true);
    deckDockWidget->setVisible(dockToActions.value(deckDockWidget).aVisible->isChecked());
}

/** @brief Creates a new deck. Handles opening in new tab if needed. */
void AbstractTabDeckEditor::actNewDeck()
{
    confirmOpen(false, [this](DeckOpenLocation deckOpenLocation) {
        if (deckOpenLocation == CANCELLED) {
            return;
        }

        if (deckOpenLocation == NEW_TAB) {
            emit openDeckEditor(LoadedDeck());
            return;
        }

        cleanDeckAndResetModified();
    });
}

/** @brief Clears the current deck and resets modified flag. */
void AbstractTabDeckEditor::cleanDeckAndResetModified()
{
    deckStateManager->clearDeck();
    deckMenu->setSaveStatus(false);
}

/**
 * @brief Displays the save confirmation dialog before loading a deck.
 * @param openInSameTabIfBlank Open in same tab if current tab is blank.
 * @param then Told where to open the deck, once the user answered.
 */
void AbstractTabDeckEditor::confirmOpen(const bool openInSameTabIfBlank,
                                        std::function<void(DeckOpenLocation location)> then)
{
    unsavedDeckGuard->confirmOpen(openInSameTabIfBlank, std::move(then));
}

/**
 * @brief Loads a deck from file using a dialog.
 * Displays a save confirmation if needed.
 */
void AbstractTabDeckEditor::actLoadDeck()
{
    confirmOpen(true, [this](DeckOpenLocation deckOpenLocation) {
        if (deckOpenLocation == CANCELLED) {
            return;
        }

        auto *dialog = new DlgLoadDeck(this);
        Dialogs::open(dialog, [this, dialog, deckOpenLocation](int result) {
            if (result == QDialog::Accepted) {
                openDeckFromFile(dialog->selectedFiles().at(0), deckOpenLocation);
            }
        });
    });
}

/**
 * @brief Opens a recently used deck file.
 * @param fileName Path to the deck file.
 */
void AbstractTabDeckEditor::actOpenRecent(const QString &fileName)
{
    confirmOpen(true, [this, fileName](DeckOpenLocation deckOpenLocation) {
        if (deckOpenLocation != CANCELLED) {
            openDeckFromFile(fileName, deckOpenLocation);
        }
    });
}

/**
 * @brief Actually opens a deck from file.
 * @param fileName Path to the deck file.
 * @param deckOpenLocation Where to open the deck (same or new tab).
 */
void AbstractTabDeckEditor::openDeckFromFile(const QString &fileName, DeckOpenLocation deckOpenLocation)
{
    DeckFileFormat::Format fmt = DeckFileFormat::getFormatFromName(fileName);

    std::optional<LoadedDeck> deckOpt = DeckLoader::loadFromFile(fileName, fmt, true);
    if (deckOpt) {
        if (deckOpenLocation == NEW_TAB) {
            emit openDeckEditor(deckOpt.value());
        } else {
            deckMenu->setSaveStatus(false);
            openDeck(deckOpt.value());
        }
    } else {
        Dialogs::critical(this, tr("Error"), tr("Could not open deck at %1").arg(fileName));
    }
    deckMenu->setSaveStatus(true);
}

/**
 * @brief Saves the current deck.
 * If remote deck, sends upload command.
 * @param then Told whether the save succeeded; a remote deck counts as saved once it is sent.
 */
void AbstractTabDeckEditor::actSaveDeck(std::function<void(bool saved)> then)
{
    if (unsavedDeckGuard->raiseOpenDialog()) {
        reportSaved(then, false);
        return;
    }
    const auto loadedDeck = deckStateManager->toLoadedDeck();
    if (loadedDeck.lastLoadInfo.remoteDeckId != LoadedDeck::LoadInfo::NON_REMOTE_ID) {
        QString deckString = loadedDeck.deckList.writeToString_Native();
        if (deckString.length() > MAX_FILE_LENGTH) {
            Dialogs::critical(this, tr("Error"), tr("Could not save remote deck"), [then] { reportSaved(then, false); });
            return;
        }

        Command_DeckUpload cmd;
        cmd.set_deck_id(static_cast<google::protobuf::uint32>(loadedDeck.lastLoadInfo.remoteDeckId));
        cmd.set_deck_list(deckString.toStdString());
        cmd.set_color_identity(getDeckColorIdentity(loadedDeck.deckList, CardDatabaseManager::query()).toStdString());

        PendingCommand *pend = AbstractClient::prepareSessionCommand(cmd);
        connect(pend, &PendingCommand::finished, this, &AbstractTabDeckEditor::saveDeckRemoteFinished);
        tabSupervisor->getClient()->sendCommand(pend);

        reportSaved(then, true);
        return;
    }
    if (loadedDeck.lastLoadInfo.fileName.isEmpty()) {
        actSaveDeckAs(std::move(then));
        return;
    }

    if (DeckLoader::saveToFile(loadedDeck)) {
        deckStateManager->setModified(false);
        reportSaved(then, true);
        return;
    }

    Dialogs::critical(
        this, tr("Error"),
        tr("The deck could not be saved.\nPlease check that the directory is writable and try again."),
        [then] { reportSaved(then, false); });
}

/**
 * @brief Saves the deck to a user-selected file.
 * @param then Told whether the save succeeded.
 */
void AbstractTabDeckEditor::actSaveDeckAs(std::function<void(bool saved)> then)
{
    if (unsavedDeckGuard->raiseOpenDialog()) {
        reportSaved(then, false);
        return;
    }
    DeckList deckList = deckStateManager->getDeckList();

    auto *dialog = new QFileDialog(this, tr("Save deck"));
    dialog->setDirectory(SettingsCache::instance().paths().getDeckPath());
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setDefaultSuffix("cod");
    dialog->setNameFilters(DeckLoader::FILE_NAME_FILTERS);
    dialog->selectFile(deckList.getName().trimmed());

    Dialogs::open(dialog, [this, dialog, deckList, then = std::move(then)](int result) {
        if (result == QDialog::Accepted) {
            saveDeckToFile(deckList, dialog->selectedFiles().at(0), then);
        } else {
            reportSaved(then, false);
        }
    });
}

void AbstractTabDeckEditor::saveDeckToFile(const DeckList &deckList,
                                           const QString &fileName,
                                           const std::function<void(bool saved)> &then)
{
    DeckFileFormat::Format fmt = DeckFileFormat::getFormatFromName(fileName);

    std::optional<LoadedDeck::LoadInfo> infoOpt = DeckLoader::saveToFile(deckList, fileName, fmt);
    if (!infoOpt) {
        Dialogs::critical(
            this, tr("Error"),
            tr("The deck could not be saved.\nPlease check that the directory is writable and try again."),
            [then] { reportSaved(then, false); });
        return;
    }

    deckStateManager->setLastLoadInfo(infoOpt.value());

    deckStateManager->setModified(false);
    SettingsCache::instance().recents().updateRecentlyOpenedDeckPaths(fileName);
    reportSaved(then, true);
}

/**
 * @brief Opens the deck share dialog with the current deck preselected.
 */
void AbstractTabDeckEditor::actShareDeck()
{
    AbstractClient *client = tabSupervisor->getServerClient();
    if (client->getStatus() != StatusLoggedIn) {
        Dialogs::information(this, tr("Share deck"), tr("You must be connected to the server to share a deck."));
        return;
    }

    const QSharedPointer<DeckList> deck = deckStateManager->getDeckListShared();
    if (deck->isBlankDeck()) {
        Dialogs::information(this, tr("Share deck"), tr("The deck is empty. Add cards before sharing it."));
        return;
    }

    Dialogs::open(new DlgShareDeck(client, deck, this), {});
}

/**
 * @brief Callback for remote deck save completion.
 * @param response Server response.
 */
void AbstractTabDeckEditor::saveDeckRemoteFinished(const Response &response)
{
    if (response.response_code() != Response::RespOk) {
        Dialogs::critical(this, tr("Error"), tr("The deck could not be saved."));
    } else {
        deckStateManager->setModified(false);
    }
}

/**
 * @brief Loads a deck from clipboard.
 * Displays confirmation if the tab is modified.
 */
void AbstractTabDeckEditor::actLoadDeckFromClipboard()
{
    confirmOpen(true, [this](DeckOpenLocation deckOpenLocation) {
        if (deckOpenLocation == CANCELLED) {
            return;
        }

        auto *dlg = new DlgLoadDeckFromClipboard(this);
        Dialogs::open(dlg, [this, dlg, deckOpenLocation](int result) {
            if (result == QDialog::Accepted) {
                openDeckList(dlg->getDeckList(), deckOpenLocation);
            }
        });
    });
}

void AbstractTabDeckEditor::openDeckList(const DeckList &deckList, DeckOpenLocation deckOpenLocation)
{
    if (deckOpenLocation == NEW_TAB) {
        emit openDeckEditor({.deckList = deckList});
    } else {
        setDeck({.deckList = deckList});
        deckStateManager->setModified(true);
    }

    deckMenu->setSaveStatus(true);
}

/**
 * @brief Opens the deck editor to edit clipboard contents.
 * @param annotated If true, edits with annotations.
 */
void AbstractTabDeckEditor::editDeckInClipboard(bool annotated)
{
    LoadedDeck loadedDeck = deckStateManager->toLoadedDeck();
    auto *dlg = new DlgEditDeckInClipboard(loadedDeck.deckList, annotated, this);
    Dialogs::open(dlg, [this, dlg, lastLoadInfo = loadedDeck.lastLoadInfo](int result) {
        if (result != QDialog::Accepted) {
            return;
        }

        setDeck({dlg->getDeckList(), lastLoadInfo});
        deckStateManager->setModified(true);
        deckMenu->setSaveStatus(true);
    });
}

/** @brief Slot for editing deck in clipboard with annotations. */
void AbstractTabDeckEditor::actEditDeckInClipboard()
{
    editDeckInClipboard(true);
}

/** @brief Slot for editing deck in clipboard without annotations. */
void AbstractTabDeckEditor::actEditDeckInClipboardRaw()
{
    editDeckInClipboard(false);
}

/** @brief Saves deck to clipboard with set info and annotation. */
void AbstractTabDeckEditor::actSaveDeckToClipboard()
{
    DeckLoader::saveToClipboard(deckStateManager->getDeckList(), true, true);
}

/** @brief Saves deck to clipboard with annotation, without set info. */
void AbstractTabDeckEditor::actSaveDeckToClipboardNoSetInfo()
{
    DeckLoader::saveToClipboard(deckStateManager->getDeckList(), true, false);
}

/** @brief Saves deck to clipboard without annotations, with set info. */
void AbstractTabDeckEditor::actSaveDeckToClipboardRaw()
{
    DeckLoader::saveToClipboard(deckStateManager->getDeckList(), false, true);
}

/** @brief Saves deck to clipboard without annotations or set info. */
void AbstractTabDeckEditor::actSaveDeckToClipboardRawNoSetInfo()
{
    DeckLoader::saveToClipboard(deckStateManager->getDeckList(), false, false);
}

/** @brief Prints the deck using a QPrintPreviewDialog. */
void AbstractTabDeckEditor::actPrintDeck()
{
    auto *dlg = new QPrintPreviewDialog(this);
    connect(dlg, &QPrintPreviewDialog::paintRequested, this,
            [this](QPrinter *printer) { DeckLoader::printDeckList(printer, deckStateManager->getDeckList()); });
    Dialogs::open(dlg, {});
}

/**
 * @brief Loads a deck from a website.
 */
void AbstractTabDeckEditor::actLoadDeckFromWebsite()
{
    confirmOpen(true, [this](DeckOpenLocation deckOpenLocation) {
        if (deckOpenLocation == CANCELLED) {
            return;
        }

        auto *dlg = new DlgLoadDeckFromWebsite(this);
        Dialogs::open(dlg, [this, dlg, deckOpenLocation](int result) {
            if (result == QDialog::Accepted) {
                openDeckList(dlg->getDeck(), deckOpenLocation);
            }
        });
    });
}

/**
 * @brief Exports the deck to a decklist website.
 * @param website Website to export to.
 */
void AbstractTabDeckEditor::exportToDecklistWebsite(DeckLoader::DecklistWebsite website)
{
    QString decklistUrlString = DeckLoader::exportDeckToDecklist(deckStateManager->getDeckList(), website);
    // Check to make sure the string isn't empty.
    if (decklistUrlString.isEmpty()) {
        // Show an error if the deck is empty, and return.
        Dialogs::critical(this, tr("Error"), tr("There are no cards in your deck to be exported"));
        return;
    }

    // Encode the string recieved from the model to make sure all characters are encoded.
    // first we put it into a qurl object
    QUrl decklistUrl = QUrl(decklistUrlString);
    // we get the correctly encoded url.
    decklistUrlString = decklistUrl.toEncoded();
    // We open the url in the user's default browser
    QDesktopServices::openUrl(decklistUrlString);
}

/** @brief Exports deck to www.decklist.org. */
void AbstractTabDeckEditor::actExportDeckDecklist()
{
    exportToDecklistWebsite(DeckLoader::DecklistOrg);
}

/** @brief Exports deck to www.decklist.xyz. */
void AbstractTabDeckEditor::actExportDeckDecklistXyz()
{
    exportToDecklistWebsite(DeckLoader::DecklistXyz);
}

/** @brief Analyzes the deck using DeckStats. */
void AbstractTabDeckEditor::actAnalyzeDeckDeckstats()
{
    auto *interface = new DeckStatsInterface(this);
    interface->analyzeDeck(deckStateManager->getDeckList());
}

/** @brief Analyzes the deck using TappedOut. */
void AbstractTabDeckEditor::actAnalyzeDeckTappedout()
{
    auto *interface = new TappedOutInterface(this);
    interface->analyzeDeck(deckStateManager->getDeckList());
}

/** @brief Applies a new filter tree to the database display. */
void AbstractTabDeckEditor::filterTreeChanged(FilterTree *filterTree)
{
    cardDatabaseDockWidget->setFilterTree(filterTree);
}

/**
 * @brief Handles the close event of the tab.
 * @param event Close event.
 */
void AbstractTabDeckEditor::closeEvent(QCloseEvent *event)
{
    emit deckEditorClosing(this);
    event->accept();
}

/** @brief Shows a confirmation dialog before closing. */
void AbstractTabDeckEditor::confirmClose(std::function<void(bool mayClose)> then)
{
    unsavedDeckGuard->confirmClose(std::move(then));
}

/** @brief Handles close requests from outside (tab manager). */
void AbstractTabDeckEditor::closeRequest()
{
    confirmClose([this](bool mayClose) {
        if (mayClose) {
            close();
        }
    });
}

void AbstractTabDeckEditor::showPrintingSelector()
{
    printingSelectorDockWidget->printingSelector->setCard(cardInfoDockWidget->cardInfo->getCard().getCardPtr());
    printingSelectorDockWidget->printingSelector->updateDisplay();
    printingSelectorDockWidget->setVisible(true);
}

void AbstractTabDeckEditor::openEdhrecTab(const CardInfoPtr &info, bool isCommander)
{
    getTabSupervisor()->addEdhrecTab(info, isCommander);
}

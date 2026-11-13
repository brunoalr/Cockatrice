#include "unsaved_deck_guard.h"

#include "../../../client/settings/cache_settings.h"
#include "../deck_editor/deck_state_manager.h"
#include "../utility/non_blocking_dialogs.h"

#include <QDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>
#include <libcockatrice/settings/deck_editor_settings.h>

UnsavedDeckGuard::UnsavedDeckGuard(QWidget *_tab,
                                   const DeckStateManager *_deck,
                                   std::function<void()> _showTab,
                                   Save _saveDeck)
    : tab(_tab), deck(_deck), showTab(std::move(_showTab)), saveDeck(std::move(_saveDeck))
{
}

void UnsavedDeckGuard::confirmClose(std::function<void(bool mayClose)> then) const
{
    if (raiseOpenDialog()) {
        then(false);
        return;
    }
    if (!deck->isModified()) {
        then(true);
        return;
    }

    showTab();
    QMessageBox *box = createSaveConfirmationWindow();
    Dialogs::open(box, [box, save = saveDeck, then = std::move(then)](QAbstractButton *clicked) {
        switch (box->standardButton(clicked)) {
            case QMessageBox::Save:
                save(then);
                break;
            case QMessageBox::Discard:
                then(true);
                break;
            default:
                then(false);
        }
    });
}

void UnsavedDeckGuard::confirmOpen(bool openInSameTabIfBlank, std::function<void(DeckOpenLocation location)> then) const
{
    if (raiseOpenDialog()) {
        then(CANCELLED);
        return;
    }
    if (SettingsCache::instance().deckEditor().getOpenDeckInNewTab()) {
        then(openInSameTabIfBlank && deck->isBlankNewDeck() ? SAME_TAB : NEW_TAB);
        return;
    }
    if (!deck->isModified()) {
        then(SAME_TAB);
        return;
    }

    showTab();
    QMessageBox *box = createSaveConfirmationWindow();
    const QPushButton *newTabButton = box->addButton(tr("Open in new tab"), QMessageBox::ApplyRole);
    Dialogs::open(box, [box, newTabButton, save = saveDeck, then = std::move(then)](QAbstractButton *clicked) {
        if (clicked == newTabButton) {
            then(NEW_TAB);
            return;
        }
        switch (box->standardButton(clicked)) {
            case QMessageBox::Save:
                save([then](bool saved) { then(saved ? SAME_TAB : CANCELLED); });
                break;
            case QMessageBox::Discard:
                then(SAME_TAB);
                break;
            default:
                then(CANCELLED);
        }
    });
}

bool UnsavedDeckGuard::raiseOpenDialog() const
{
    for (QDialog *dialog : tab->findChildren<QDialog *>(Qt::FindDirectChildrenOnly)) {
        if (dialog->isVisible()) {
            dialog->raise();
            dialog->activateWindow();
            return true;
        }
    }
    return false;
}

QMessageBox *UnsavedDeckGuard::createSaveConfirmationWindow() const
{
    auto *msgBox = new QMessageBox(tab);
    msgBox->setIcon(QMessageBox::Warning);
    msgBox->setWindowTitle(tr("Are you sure?"));
    msgBox->setText(tr("The decklist has been modified.\nDo you want to save the changes?"));
    msgBox->setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    return msgBox;
}

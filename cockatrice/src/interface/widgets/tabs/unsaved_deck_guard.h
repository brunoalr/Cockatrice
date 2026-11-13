#ifndef UNSAVED_DECK_GUARD_H
#define UNSAVED_DECK_GUARD_H

#include <QCoreApplication>
#include <functional>

class DeckStateManager;
class QMessageBox;
class QWidget;

class UnsavedDeckGuard
{
    // The shipped translations of these strings are filed under the deck editor tab.
    Q_DECLARE_TR_FUNCTIONS(AbstractTabDeckEditor)

public:
    enum DeckOpenLocation
    {
        CANCELLED,
        SAME_TAB,
        NEW_TAB
    };

    using Save = std::function<void(std::function<void(bool saved)> then)>;

    UnsavedDeckGuard(QWidget *_tab, const DeckStateManager *_deck, std::function<void()> _showTab, Save _saveDeck);

    void confirmClose(std::function<void(bool mayClose)> then) const;
    void confirmOpen(bool openInSameTabIfBlank, std::function<void(DeckOpenLocation location)> then) const;
    bool raiseOpenDialog() const;

private:
    QMessageBox *createSaveConfirmationWindow() const;

    QWidget *tab;
    const DeckStateManager *deck;
    std::function<void()> showTab;
    Save saveDeck;
};

#endif // UNSAVED_DECK_GUARD_H

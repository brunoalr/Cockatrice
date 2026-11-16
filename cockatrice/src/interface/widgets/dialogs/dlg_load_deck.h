/**
 * @file dlg_load_deck.h
 * @ingroup LocalDeckStorageDialogs
 * @ingroup Lobby
 */
//! \todo Document this file.

#ifndef DLG_LOAD_DECK_H
#define DLG_LOAD_DECK_H

#include <QFileDialog>
#include <functional>
#include <qtmetamacros.h>

class QWidget;

/**
 * The file dialog for "Load Deck" operations.
 * Handles remembering the most recently used deck loading directory.
 */
class DlgLoadDeck : public QFileDialog
{
    Q_OBJECT

    void actAccepted();

public:
    explicit DlgLoadDeck(QWidget *parent = nullptr);

    // Where device folders are out of reach, the player picks one of the decks in the decks folder instead.
    static void choose(QWidget *parent, std::function<void(const QString &fileName)> thenChosen);
};

#endif // DLG_LOAD_DECK_H

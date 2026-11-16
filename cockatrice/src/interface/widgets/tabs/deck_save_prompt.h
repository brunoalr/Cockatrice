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

#ifndef DECK_SAVE_PROMPT_H
#define DECK_SAVE_PROMPT_H

#include <QCoreApplication>
#include <QString>
#include <functional>

class DeckList;
class QWidget;

class DeckSavePrompt
{
    // The shipped translations of the file dialog's title are filed under the deck editor tab.
    Q_DECLARE_TR_FUNCTIONS(AbstractTabDeckEditor)

public:
    // Where device folders are out of reach, the deck goes into the decks folder under a name the player types.
    // Reports an empty name when the player cancelled.
    static void chooseFile(QWidget *parent, const QString &deckName, std::function<void(const QString &fileName)> then);

    static void importFromDevice(QWidget *parent, std::function<void(const QString &path)> thenImported);
    static void exportToDevice(const DeckList &deck, const QString &fileName);

private:
    static void askForName(QWidget *parent, const QString &typed, std::function<void(const QString &fileName)> then);
};

#endif // DECK_SAVE_PROMPT_H

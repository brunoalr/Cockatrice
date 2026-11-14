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

#include "settings_close_check.h"

#include "../../../client/settings/cache_settings.h"
#include "../utility/non_blocking_dialogs.h"

#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QMessageBox>
#include <libcockatrice/card/database/card_database.h>
#include <libcockatrice/card/database/card_database_loader.h>
#include <libcockatrice/card/database/card_database_manager.h>
#include <libcockatrice/settings/paths_settings.h>
#include <utility>

namespace
{
bool isUnusableDir(const QString &path)
{
    return !QDir(path).exists() || path.isEmpty();
}
} // namespace

SettingsCloseCheck::SettingsCloseCheck(QDialog *_dialog) : QObject(_dialog), dialog(_dialog)
{
    dialog->installEventFilter(this);
}

bool SettingsCloseCheck::eventFilter(QObject *, QEvent *event)
{
    if (event->type() != QEvent::Close || mayClose()) {
        return false;
    }
    event->ignore();
    return true;
}

bool SettingsCloseCheck::mayClose()
{
    using enum ShutdownChain::State;
    switch (chain.state()) {
        case Accepted:
            return true;
        case Confirming:
            return false;
        case Idle:
            break;
    }

    bool showLoadError = true;
    QString loadErrorMessage = tr("Unknown Error loading card database");
    const LoadStatus loadStatus = CardDatabaseManager::getInstance()->getLoadStatus();
    qCInfo(DlgSettingsLog) << "Card Database load status: " << loadStatus;
    switch (loadStatus) {
        case Ok:
            showLoadError = false;
            break;
        case Invalid:
            loadErrorMessage = tr("Your card database is invalid.\n\n"
                                  "Cockatrice may not function correctly with an invalid database\n\n"
                                  "You may need to rerun Oracle to update your card database.\n\n"
                                  "Would you like to change your database location setting?");
            break;
        case VersionTooOld:
            loadErrorMessage = tr("Your card database version is too old.\n\n"
                                  "This can cause problems loading card information or images\n\n"
                                  "Usually this can be fixed by rerunning Oracle to to update your card database.\n\n"
                                  "Would you like to change your database location setting?");
            break;
        case NotLoaded:
            loadErrorMessage = tr("Your card database did not finish loading\n\n"
                                  "Please file a ticket at https://github.com/Cockatrice/Cockatrice/issues with your "
                                  "cards.xml attached\n\n"
                                  "Would you like to change your database location setting?");
            break;
        case FileError:
            loadErrorMessage = tr("File Error loading your card database.\n\n"
                                  "Would you like to change your database location setting?");
            break;
        case NoCards:
            loadErrorMessage = tr("Your card database was loaded but contains no cards.\n\n"
                                  "Would you like to change your database location setting?");
            break;
        default:
            loadErrorMessage = tr("Unknown card database load status\n\n"
                                  "Please file a ticket at https://github.com/Cockatrice/Cockatrice/issues\n\n"
                                  "Would you like to change your database location setting?");

            break;
    }

    // With nothing to ask the close is agreed here; otherwise the dialog closes again once every answer is No.
    chain.start(
        {
            askToGoBackIf([showLoadError] { return showLoadError; }, loadErrorMessage),
            askToGoBackIf([] { return isUnusableDir(SettingsCache::instance().paths().getDeckPath()); },
                          tr("The path to your deck directory is invalid. Would you like to go back and set the "
                             "correct path?")),
            askToGoBackIf([] { return isUnusableDir(SettingsCache::instance().paths().getPicsPath()); },
                          tr("The path to your card pictures directory is invalid. Would you like to go back "
                             "and set the correct path?")),
        },
        [this](bool accepted) {
            if (accepted) {
                dialog->close();
            }
        });
    return chain.state() == ShutdownChain::State::Accepted;
}

ShutdownChain::Step SettingsCloseCheck::askToGoBackIf(std::function<bool()> unusable, const QString &question)
{
    return {dialog, [this, unusable = std::move(unusable), question](const ShutdownChain::Answer &answer) {
                if (!unusable()) {
                    answer(true);
                    return;
                }
                Dialogs::ask(dialog, QMessageBox::Critical, tr("Error"), question, QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::NoButton,
                             [answer](QMessageBox::StandardButton button) { answer(button != QMessageBox::Yes); });
            }};
}

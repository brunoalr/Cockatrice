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

#include "deck_save_prompt.h"

#include "../../../client/settings/cache_settings.h"
#include "../../deck_loader/deck_loader.h"
#include "../utility/non_blocking_dialogs.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <libcockatrice/settings/paths_settings.h>

namespace
{
QString safeFileName(const QString &typed, const QString &fallback)
{
    QString name;
    for (const QChar c : typed) {
        if (c.category() != QChar::Other_Control && !QStringLiteral("/\\:*?\"<>|").contains(c)) {
            name += c;
        }
    }
    // A leading dot hides the file, and Windows refuses a trailing one.
    const auto isEdge = [](QChar c) { return c.isSpace() || c == '.'; };
    const auto trim = [&name, isEdge] {
        while (!name.isEmpty() && isEdge(name.back())) {
            name.chop(1);
        }
        while (!name.isEmpty() && isEdge(name.front())) {
            name.remove(0, 1);
        }
    };
    trim();
    if (name.isEmpty()) {
        name = fallback;
    }
    QString suffix = ".cod";
    const QString typedSuffix = QFileInfo(name).suffix();
    if (DeckLoader::ACCEPTED_FILE_EXTENSIONS.contains("*." + typedSuffix, Qt::CaseInsensitive)) {
        suffix = "." + typedSuffix;
        name.chop(suffix.size());
    }
    // File systems take at most 255 bytes, and the browser's lists skip a longer name.
    while ((name + suffix).toUtf8().size() > 255) {
        name.chop(name.back().isLowSurrogate() ? 2 : 1);
        trim();
    }
    return name + suffix;
}
} // namespace

void DeckSavePrompt::chooseFile(QWidget *parent,
                                const QString &deckName,
                                std::function<void(const QString &fileName)> then)
{
    if (!SettingsCache::instance().canReachDeviceFolders()) {
        askForName(parent, deckName.isEmpty() ? tr("Untitled deck") : deckName, std::move(then));
        return;
    }

    auto *dialog = new QFileDialog(parent, tr("Save deck"));
    dialog->setDirectory(SettingsCache::instance().paths().getDeckPath());
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setDefaultSuffix("cod");
    dialog->setNameFilters(DeckLoader::FILE_NAME_FILTERS);
    dialog->selectFile(deckName);
    Dialogs::open(dialog, [dialog, then = std::move(then)](int result) {
        then(result == QDialog::Accepted ? dialog->selectedFiles().at(0) : QString());
    });
}

void DeckSavePrompt::askForName(QWidget *parent,
                                const QString &typed,
                                std::function<void(const QString &fileName)> then)
{
    auto *prompt = new QInputDialog(parent);
    prompt->setWindowTitle(tr("Save deck"));
    prompt->setLabelText(tr("Name of the deck file:"));
    prompt->setTextValue(typed);
    Dialogs::open(prompt, [parent, prompt, then = std::move(then)](int result) {
        if (result != QDialog::Accepted) {
            then({});
            return;
        }
        const QString answer = prompt->textValue();
        const QString fileName = safeFileName(answer, tr("Untitled deck"));
        const QString path = QDir(SettingsCache::instance().paths().getDeckPath()).filePath(fileName);
        if (!QFileInfo::exists(path)) {
            then(path);
            return;
        }
        Dialogs::confirm(
            parent, tr("Save deck"), tr("A deck file named \"%1\" already exists. Replace it?").arg(fileName),
            [parent, answer, path, then](bool replace) {
                if (replace) {
                    then(path);
                } else {
                    askForName(parent, answer, then);
                }
            },
            QMessageBox::No);
    });
}

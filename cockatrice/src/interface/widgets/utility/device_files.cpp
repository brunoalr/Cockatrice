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

#include "device_files.h"

#include "../../../client/flush_player_data.h"
#include "../../../client/settings/cache_settings.h"
#include "non_blocking_dialogs.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>
#include <utility>

namespace
{
struct Replacements
{
    DeviceFiles::Open open;
    DeviceFiles::Save save;
};

Replacements &replacements()
{
    static Replacements replaced;
    return replaced;
}
} // namespace

void DeviceFiles::open(QWidget *parent,
                       const QString &caption,
                       const QString &startFolder,
                       const QString &nameFilter,
                       const Opened &thenPicked)
{
    if (SettingsCache::instance().canReachDeviceFolders()) {
        auto *dialog = new QFileDialog(parent, caption, startFolder, nameFilter);
        dialog->setFileMode(QFileDialog::ExistingFile);
        Dialogs::open(dialog, [dialog, thenPicked](int result) {
            if (result != QDialog::Accepted) {
                return;
            }
            const QString fileName = dialog->selectedFiles().at(0);
            QFile file(fileName);
            thenPicked(fileName, file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray());
        });
        return;
    }
    // A browser reports a cancel as an empty name; the picker cannot be closed when its asker goes.
    const Opened picked = [guard = QPointer<QObject>(parent), thenPicked](const QString &fileName,
                                                                          const QByteArray &bytes) {
        if (guard && !fileName.isEmpty()) {
            thenPicked(fileName, bytes);
        }
    };
    if (replacements().open) {
        replacements().open(nameFilter, picked);
        return;
    }
    QFileDialog::getOpenFileContent(nameFilter, picked);
}

void DeviceFiles::save(const QByteArray &bytes, const QString &fileNameHint)
{
    if (replacements().save) {
        replacements().save(bytes, fileNameHint);
        return;
    }
    QFileDialog::saveFileContent(bytes, fileNameHint);
}

void DeviceFiles::replaceForTesting(Open open, Save save)
{
    replacements() = {std::move(open), std::move(save)};
}

QString DeviceFiles::safeFileName(const QString &typed, const QString &fallback, const QStringList &extensions)
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
    QString suffix = extensions.first().mid(1);
    const QString typedSuffix = QFileInfo(name).suffix();
    if (extensions.contains("*." + typedSuffix, Qt::CaseInsensitive)) {
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

void DeviceFiles::storeAs(QWidget *parent,
                          const QString &path,
                          const QByteArray &bytes,
                          const std::function<void(const QString &path)> &thenStored)
{
    const auto store = [parent, path, bytes, thenStored] {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            Dialogs::critical(
                parent, QCoreApplication::translate("DeviceFiles", "Error"),
                QCoreApplication::translate("DeviceFiles",
                                            "The file could not be saved.\nPlease check that the directory is writable "
                                            "and try again."));
            return;
        }
        flushPlayerData();
        if (thenStored) {
            thenStored(path);
        }
    };
    if (!QFileInfo::exists(path)) {
        store();
        return;
    }
    Dialogs::confirm(
        parent, QCoreApplication::translate("DeviceFiles", "Import from device"),
        QCoreApplication::translate("DeviceFiles", "A file named \"%1\" already exists. Replace it?")
            .arg(QFileInfo(path).fileName()),
        [store](bool replace) {
            if (replace) {
                store();
            }
        },
        QMessageBox::No);
}

void DeviceFiles::exportFile(QWidget *parent, const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        Dialogs::critical(parent, QCoreApplication::translate("DeviceFiles", "Error"),
                          QCoreApplication::translate("DeviceFiles", "Could not read %1").arg(path));
        return;
    }
    save(file.readAll(), QFileInfo(path).fileName());
}

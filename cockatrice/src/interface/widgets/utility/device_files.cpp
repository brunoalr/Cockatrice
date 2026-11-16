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

#include <QFileDialog>
#include <QPointer>
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

void DeviceFiles::open(QObject *context, const QString &nameFilter, const Opened &thenPicked)
{
    // A browser reports a cancel as an empty name; the picker cannot be closed when its asker goes.
    const Opened picked = [guard = QPointer<QObject>(context), thenPicked](const QString &fileName,
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

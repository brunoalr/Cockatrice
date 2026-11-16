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

#ifndef DEVICE_FILES_H
#define DEVICE_FILES_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

class QFileSystemModel;
class QItemSelectionModel;
class QWidget;

namespace DeviceFiles
{
using Opened = std::function<void(const QString &fileName, const QByteArray &bytes)>;
using Open = std::function<void(const QString &nameFilter, const Opened &then)>;
using Save = std::function<void(const QByteArray &bytes, const QString &fileNameHint)>;

// Where device folders are reachable, a file dialog; elsewhere the browser's picker, which takes the filter only.
// Bytes that are empty mean the file could not be read.
void open(QWidget *parent,
          const QString &caption,
          const QString &startFolder,
          const QString &nameFilter,
          const Opened &thenPicked);
void save(const QByteArray &bytes, const QString &fileNameHint);

void replaceForTesting(Open open, Save save);

// Without one of the extensions the name gets the first.
QString safeFileName(const QString &typed, const QString &fallback, const QStringList &extensions);
void storeAs(QWidget *parent,
             const QString &path,
             const QByteArray &bytes,
             const std::function<void(const QString &path)> &thenStored);
void exportFile(QWidget *parent, const QString &path);
// Empty unless exactly one row is selected and it is a file: a browser opens one save picker per click.
QString singleSelectedFile(const QItemSelectionModel *selection, const QFileSystemModel *files);
} // namespace DeviceFiles

#endif

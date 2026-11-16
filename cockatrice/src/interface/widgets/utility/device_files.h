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
#include <functional>

class QObject;

namespace DeviceFiles
{
using Opened = std::function<void(const QString &fileName, const QByteArray &bytes)>;
using Open = std::function<void(const QString &nameFilter, const Opened &then)>;
using Save = std::function<void(const QByteArray &bytes, const QString &fileNameHint)>;

void open(QObject *context, const QString &nameFilter, const Opened &thenPicked);
void save(const QByteArray &bytes, const QString &fileNameHint);

void replaceForTesting(Open open, Save save);
} // namespace DeviceFiles

#endif

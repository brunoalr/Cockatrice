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

#ifndef WEB_RELAY_H
#define WEB_RELAY_H

#include <QString>
#include <QUrl>

namespace WebRelay
{
QUrl route(const QUrl &url);
bool reaches(const QUrl &url);

namespace detail
{
QUrl parseRelay(const QString &text);
bool isRelayed(const QUrl &url);
QUrl routeVia(const QUrl &relay, const QUrl &url);
} // namespace detail
} // namespace WebRelay

#endif // WEB_RELAY_H

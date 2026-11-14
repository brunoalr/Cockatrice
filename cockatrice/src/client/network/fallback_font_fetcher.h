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

#ifndef FALLBACK_FONT_FETCHER_H
#define FALLBACK_FONT_FETCHER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class FallbackFontFetcher : public QObject
{
    Q_OBJECT

public:
    static constexpr int maxFonts = 8;
    static constexpr qint64 maxFontBytes = 32 * 1024 * 1024;

    explicit FallbackFontFetcher(const QUrl &page, QObject *parent = nullptr);

    void fetch();

signals:
    void finished();

private:
    QNetworkReply *get(const QString &name, qint64 maxBytes);
    void manifestFetched(QNetworkReply *reply);
    void fetchNext();
    void addFetched();

    QNetworkAccessManager *network;
    QUrl fonts;
    QStringList pending;
    QList<QPair<QString, QByteArray>> fetched;
    QElapsedTimer elapsed;
};

#endif // FALLBACK_FONT_FETCHER_H

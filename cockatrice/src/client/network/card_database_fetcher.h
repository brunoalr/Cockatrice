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

#ifndef CARD_DATABASE_FETCHER_H
#define CARD_DATABASE_FETCHER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QUrl>

inline Q_LOGGING_CATEGORY(CardDatabaseFetcherLog, "card_database.fetcher");

class QNetworkAccessManager;
class QNetworkReply;

class CardDatabaseFetcher : public QObject
{
    Q_OBJECT

public:
    CardDatabaseFetcher(const QUrl &base, const QString &targetDir, QObject *parent = nullptr);

    void fetch();

signals:
    void fetched();
    void failed(const QString &what);

private:
    void replyFinished(QNetworkReply *reply, const QString &name);
    void save();
    void fail(const QString &what);

    QNetworkAccessManager *network;
    QUrl base;
    QString targetDir;
    QList<QNetworkReply *> running;
    QHash<QString, QByteArray> downloaded;
    QElapsedTimer elapsed;
};

#endif // CARD_DATABASE_FETCHER_H

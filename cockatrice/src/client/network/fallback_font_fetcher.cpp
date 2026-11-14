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

#include "fallback_font_fetcher.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFontDatabase>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QWidget>
#include <libcockatrice/network/network_manager.h>
#include <utility>
#ifdef Q_OS_WASM
#include "../settings/cache_settings.h"

#include <QHash>
#include <libcockatrice/settings/personal_settings.h>
#endif

Q_LOGGING_CATEGORY(FallbackFontFetcherLog, "fallback_font_fetcher");

namespace
{
const qint64 maxManifestBytes = 4096;
// A browser applies this limit to the whole download, not to a stall.
const int wholeDownloadLimitMs = 10 * 60 * 1000;
} // namespace

FallbackFontFetcher::FallbackFontFetcher(const QUrl &page, QObject *parent)
    : QObject(parent), network(NetworkManagerFactory::create(this)),
      fonts(page.resolved(QUrl(QStringLiteral("fonts/"))))
{
}

void FallbackFontFetcher::fetch()
{
    if (elapsed.isValid()) {
        return;
    }
    elapsed.start();
    QNetworkReply *reply = get(QStringLiteral("index.txt"), maxManifestBytes);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { manifestFetched(reply); });
}

QNetworkReply *FallbackFontFetcher::get(const QString &name, qint64 maxBytes)
{
    QNetworkRequest request(fonts.resolved(QUrl(name)));
    request.setTransferTimeout(wholeDownloadLimitMs);
    QNetworkReply *reply = network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64 total) {
        if (received > maxBytes || total > maxBytes) {
            reply->abort();
        }
    });
    return reply;
}

void FallbackFontFetcher::manifestFetched(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        qCInfo(FallbackFontFetcherLog) << "No fallback fonts:" << reply->errorString();
        emit finished();
        return;
    }
    // A browser counts the compressed bytes while a download runs, so only the decoded size is certain.
    const QByteArray manifest = reply->readAll();
    if (manifest.size() > maxManifestBytes) {
        qCWarning(FallbackFontFetcherLog)
            << "No fallback fonts: the list is larger than" << maxManifestBytes << "bytes";
        emit finished();
        return;
    }
    // A name with no slash, colon, percent sign or leading dot resolves inside fonts/ and nowhere else.
    static const QRegularExpression plainName(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    QStringList refused;
    for (const QString &line : QString::fromUtf8(manifest).split(QLatin1Char('\n'))) {
        const QString name = line.trimmed();
        if (name.isEmpty()) {
            continue;
        }
        if (plainName.match(name).hasMatch() && pending.size() < maxFonts) {
            pending.append(name);
        } else {
            refused.append(name);
        }
    }
    if (!refused.isEmpty()) {
        qCWarning(FallbackFontFetcherLog) << "Fallback font entries refused:" << refused;
    }
    fetchNext();
}

void FallbackFontFetcher::fetchNext()
{
    if (pending.isEmpty()) {
        addFetched();
        emit finished();
        return;
    }
    const QString name = pending.takeFirst();
    QNetworkReply *reply = get(name, maxFontBytes);
    connect(reply, &QNetworkReply::finished, this, [this, reply, name] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qCInfo(FallbackFontFetcherLog) << "Fallback font" << name << "skipped:" << reply->errorString();
        } else if (const QByteArray data = reply->readAll(); data.size() > maxFontBytes) {
            qCWarning(FallbackFontFetcherLog)
                << "Fallback font" << name << "skipped: larger than" << maxFontBytes << "bytes";
        } else {
            fetched.append({name, data});
        }
        fetchNext();
    });
}

// Qt 6.11 keeps the font engines that existing text took after the first font was added: a later font
// would not reach that text. So the fonts are added together, once the last one is downloaded.
void FallbackFontFetcher::addFetched()
{
    if (fetched.isEmpty()) {
        return;
    }
#if defined(Q_OS_WASM) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // The site's collection has a face for each of these, with the Han forms the language expects.
    static const QHash<QString, QString> hanFaces = {
        {QStringLiteral("ja"), QStringLiteral("Noto Sans CJK JP")},
        {QStringLiteral("ko"), QStringLiteral("Noto Sans CJK KR")},
        {QStringLiteral("zh-Hans"), QStringLiteral("Noto Sans CJK SC")},
        {QStringLiteral("zh-Hant"), QStringLiteral("Noto Sans CJK TC")},
        {QStringLiteral("yue"), QStringLiteral("Noto Sans CJK HK")},
    };
    if (const QString face = hanFaces.value(SettingsCache::instance().personal().getLang()); !face.isEmpty()) {
        QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han, face);
    }
#endif
    bool added = false;
    for (const auto &[name, data] : std::exchange(fetched, {})) {
        if (QFontDatabase::addApplicationFontFromData(data) < 0) {
            qCWarning(FallbackFontFetcherLog) << "Fallback font" << name << "skipped: not a font";
            continue;
        }
        added = true;
        qCInfo(FallbackFontFetcherLog) << "Fallback font registered:" << name << data.size() << "bytes,"
                                       << elapsed.elapsed() << "ms after the fetch began";
    }
    if (!added) {
        return;
    }
    // A line edit keeps the boxes it laid out before the font arrived until it sees a font change,
    // and a cached board item keeps them in its stored picture until it is told to draw again.
    QEvent fontChange(QEvent::FontChange);
    for (QWidget *widget : QApplication::allWidgets()) {
        QApplication::sendEvent(widget, &fontChange);
        if (const auto *view = qobject_cast<QGraphicsView *>(widget); view && view->scene()) {
            for (QGraphicsItem *item : view->scene()->items()) {
                item->update();
            }
        }
    }
}

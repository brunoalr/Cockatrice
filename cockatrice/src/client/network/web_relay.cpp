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

#include "web_relay.h"

#include <QStringList>
#ifdef Q_OS_WASM
#include <emscripten/val.h>
#include <string>
#endif

namespace
{
const QStringList relayedHosts = {
    QStringLiteral("archidekt.com"),
    QStringLiteral("card-images.archidekt.com"),
    QStringLiteral("storage.googleapis.com"),
    QStringLiteral("cloud.manabox.app"),
    QStringLiteral("backend.commanderspellbook.com"),
};

#ifdef Q_OS_WASM
const QUrl &pageRelay()
{
    static const QUrl relay = [] {
        const emscripten::val meta =
            emscripten::val::global("document")
                .call<emscripten::val>("querySelector", std::string("meta[name=cockatrice-relay]"));
        return meta.isNull() ? QUrl()
                             : WebRelay::detail::parseRelay(QString::fromStdString(meta["content"].as<std::string>()));
    }();
    return relay;
}
#endif
} // namespace

QUrl WebRelay::route(const QUrl &url)
{
#ifdef Q_OS_WASM
    return detail::routeVia(pageRelay(), url);
#else
    return url;
#endif
}

bool WebRelay::reaches(const QUrl &url)
{
#ifdef Q_OS_WASM
    return !pageRelay().isEmpty() && detail::isRelayed(url);
#else
    Q_UNUSED(url);
    return true;
#endif
}

QUrl WebRelay::detail::parseRelay(const QString &text)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    const bool local = url.scheme() == QLatin1String("http") &&
                       (url.host() == QLatin1String("127.0.0.1") || url.host() == QLatin1String("localhost"));
    // The relay reads the upstream host from the first part of the path, so it answers only at the root of its host.
    const bool origin = url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
                        (url.path().isEmpty() || url.path() == QLatin1String("/"));
    return url.isValid() && !url.host().isEmpty() && origin && (url.scheme() == QLatin1String("https") || local)
               ? url
               : QUrl();
}

bool WebRelay::detail::isRelayed(const QUrl &url)
{
    return (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")) &&
           relayedHosts.contains(url.host());
}

QUrl WebRelay::detail::routeVia(const QUrl &relay, const QUrl &url)
{
    if (relay.isEmpty() || !isRelayed(url)) {
        return url;
    }
    QUrl routed = relay;
    routed.setPath(QLatin1Char('/') + url.host() + url.path(QUrl::FullyEncoded), QUrl::TolerantMode);
    if (url.hasQuery()) {
        routed.setQuery(url.query(QUrl::FullyEncoded), QUrl::TolerantMode);
    }
    return routed;
}

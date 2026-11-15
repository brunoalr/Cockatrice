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

#include "client/network/web_relay.h"

#include <QString>
#include <QUrl>
#include <gtest/gtest.h>

using WebRelay::detail::isRelayed;
using WebRelay::detail::parseRelay;
using WebRelay::detail::routeVia;

namespace
{
const QUrl relay(QStringLiteral("https://relay.test"));

QString routed(const char *url)
{
    return routeVia(relay, QUrl(QString::fromLatin1(url))).toString();
}
} // namespace

TEST(WebRelayTest, EachRelayedHostGoesToItsPathUnderTheRelay)
{
    EXPECT_EQ(routed("https://archidekt.com/api/decks/v3/"), "https://relay.test/archidekt.com/api/decks/v3/");
    EXPECT_EQ(routed("https://card-images.archidekt.com/a/b.png"),
              "https://relay.test/card-images.archidekt.com/a/b.png");
    EXPECT_EQ(routed("https://storage.googleapis.com/topdekt-user/images/shadow.jpg"),
              "https://relay.test/storage.googleapis.com/topdekt-user/images/shadow.jpg");
    EXPECT_EQ(routed("https://cloud.manabox.app/decks/XYZxxa-YZ99Yyyx_yzXzzz"),
              "https://relay.test/cloud.manabox.app/decks/XYZxxa-YZ99Yyyx_yzXzzz");
    EXPECT_EQ(routed("https://backend.commanderspellbook.com/estimate-bracket"),
              "https://relay.test/backend.commanderspellbook.com/estimate-bracket");
}

TEST(WebRelayTest, TheQueryIsKept)
{
    EXPECT_EQ(routed("https://archidekt.com/api/decks/v3/?orderBy=-updatedAt&colors=W%2CU&name=Sol+Ring&page=2"),
              "https://relay.test/archidekt.com/api/decks/v3/?orderBy=-updatedAt&colors=W%2CU&name=Sol+Ring&page=2");
    EXPECT_EQ(routed("https://archidekt.com/api/decks/123/?format=json"),
              "https://relay.test/archidekt.com/api/decks/123/?format=json");
}

TEST(WebRelayTest, ThePlainHttpAddressOfTheNextPageLinkIsRelayedToo)
{
    EXPECT_EQ(routed("http://archidekt.com/api/decks/v3/?page=2"),
              "https://relay.test/archidekt.com/api/decks/v3/?page=2");
}

TEST(WebRelayTest, AHostOutsideTheTableIsNotChanged)
{
    for (const char *url : {"https://moxfield.com/decks/abc", "https://api.moxfield.com/v2/decks/all/abc/",
                            "https://tappedout.net/mtg-decks/x/?fmt=txt", "https://deckstats.net/decks/1/2-x",
                            "https://json.edhrec.com/pages/top/year.json"}) {
        EXPECT_EQ(routed(url), QString::fromLatin1(url));
    }
}

TEST(WebRelayTest, AHostThatOnlyEndsWithOrStartsWithARelayedNameIsNotChanged)
{
    for (const char *url : {"https://evilarchidekt.com/api/decks/v3/", "https://archidekt.com.evil.test/api/decks/v3/",
                            "https://www.archidekt.com/api/decks/v3/", "https://evil.test/archidekt.com/api/decks/v3/",
                            "https://archidekt.com@evil.test/api/decks/v3/"}) {
        EXPECT_EQ(routed(url), QString::fromLatin1(url));
        EXPECT_FALSE(isRelayed(QUrl(QString::fromLatin1(url)))) << url;
    }
}

TEST(WebRelayTest, WithoutARelayNothingIsChanged)
{
    EXPECT_EQ(routeVia(QUrl(), QUrl("https://archidekt.com/api/decks/v3/?page=1")).toString(),
              "https://archidekt.com/api/decks/v3/?page=1");
}

TEST(WebRelayTest, ATrailingSlashOnTheRelayMakesNoDifference)
{
    const QUrl url("https://archidekt.com/api/decks/v3/?page=1");
    EXPECT_EQ(routeVia(QUrl("https://relay.test/"), url).toString(),
              "https://relay.test/archidekt.com/api/decks/v3/?page=1");
    EXPECT_EQ(routeVia(QUrl("https://relay.test"), url).toString(),
              "https://relay.test/archidekt.com/api/decks/v3/?page=1");
}

TEST(WebRelayTest, AnAddressWithoutAPathReachesTheHostRoot)
{
    EXPECT_EQ(routed("https://archidekt.com"), "https://relay.test/archidekt.com");
}

TEST(WebRelayTest, ARoutedAddressIsNotRoutedAgain)
{
    const QUrl once = routeVia(relay, QUrl("https://archidekt.com/api/decks/v3/?page=1"));
    EXPECT_EQ(routeVia(relay, once), once);
}

TEST(WebRelayTest, OnlyAnHttpsAddressIsARelay)
{
    EXPECT_EQ(parseRelay("https://relay.example").toString(), "https://relay.example");
    EXPECT_EQ(parseRelay("https://relay.example/").toString(), "https://relay.example/");
    EXPECT_EQ(parseRelay("https://relay.example:8443").toString(), "https://relay.example:8443");
    EXPECT_EQ(parseRelay("  https://relay.example  ").toString(), "https://relay.example");
    for (const char *text : {"", "relay.example", "http://relay.example", "ftp://relay.example", "javascript:alert(1)",
                             "file:///tmp/x", "https://", "https:///path", "//relay.example", "not a url"}) {
        EXPECT_TRUE(parseRelay(QString::fromLatin1(text)).isEmpty()) << text;
    }
}

// The relay reads the upstream host from the first part of the path, so it answers only at the root of its host.
TEST(WebRelayTest, ARelayAddressIsAnOriginAndNothingMore)
{
    for (const char *text : {"https://relay.example/v1", "https://relay.example/v1/", "https://relay.example/?x=1",
                             "https://relay.example?x=1", "https://relay.example/#x", "https://user@relay.example"}) {
        EXPECT_TRUE(parseRelay(QString::fromLatin1(text)).isEmpty()) << text;
    }
}

TEST(WebRelayTest, PlainHttpIsARelayOnlyOnTheLocalMachine)
{
    EXPECT_EQ(parseRelay("http://127.0.0.1:8787").toString(), "http://127.0.0.1:8787");
    EXPECT_EQ(parseRelay("http://localhost:8787").toString(), "http://localhost:8787");
    EXPECT_TRUE(parseRelay("http://127.0.0.1.evil.test").isEmpty());
    EXPECT_TRUE(parseRelay("http://localhost.evil.test").isEmpty());
}

TEST(WebRelayTest, OnTheDesktopEveryAddressIsReachedAsItIs)
{
    for (const char *url : {"https://archidekt.com/api/decks/v3/", "https://moxfield.com/decks/abc"}) {
        EXPECT_EQ(WebRelay::route(QUrl(QString::fromLatin1(url))), QUrl(QString::fromLatin1(url)));
        EXPECT_TRUE(WebRelay::reaches(QUrl(QString::fromLatin1(url)))) << url;
    }
}

TEST(WebRelayTest, EveryHostOfTheTableIsRelayedAndNoOtherIs)
{
    for (const char *host : {"archidekt.com", "card-images.archidekt.com", "storage.googleapis.com",
                             "cloud.manabox.app", "backend.commanderspellbook.com"}) {
        EXPECT_TRUE(isRelayed(QUrl(QStringLiteral("https://") + QString::fromLatin1(host) + "/x"))) << host;
    }
    for (const char *host :
         {"moxfield.com", "tappedout.net", "deckstats.net", "scryfall.com", "cockatrice.github.io"}) {
        EXPECT_FALSE(isRelayed(QUrl(QStringLiteral("https://") + QString::fromLatin1(host) + "/x"))) << host;
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

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

#include "client/settings/cache_settings.h"
#include "dialog_test_support.h"
#include "interface/deck_loader/deck_loader.h"
#include "interface/theme_manager.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <libcockatrice/deck_list/deck_file_format.h>
#include <libcockatrice/deck_list/deck_list.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <optional>

ThemeManager *themeManager = nullptr; // NOSONAR: the client declares it extern and assigns it
RNG_Abstract *rng = nullptr;          // NOSONAR: the client declares it extern and assigns it

namespace
{
DeckList burn()
{
    DeckList deck;
    deck.setName("Burn");
    deck.setComments("Fast and red");
    deck.addCards("Lightning Bolt", 4, DECK_ZONE_MAIN);
    deck.addCards("Mountain", 16, DECK_ZONE_MAIN);
    deck.addCards("Smash to Smithereens", 2, DECK_ZONE_SIDE);
    return deck;
}

class DeckBytes : public ::testing::Test // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QTemporaryDir dir;

    QByteArray written(const DeckList &deck, const QString &name, DeckFileFormat::Format format) const
    {
        const QString path = dir.filePath(name);
        EXPECT_TRUE(DeckLoader::saveToFile(deck, path, format).has_value());
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    }

    void expectSameAsFromFile(const QString &name, const QByteArray &bytes) const
    {
        const QString path = dir.filePath(name);
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        file.close();

        const auto fromFile = DeckLoader::loadFromFile(path, DeckFileFormat::getFormatFromName(path));
        const auto fromBytes = DeckLoader::loadFromBytes(name, bytes);
        ASSERT_TRUE(fromFile.has_value());
        ASSERT_TRUE(fromBytes.has_value());
        EXPECT_EQ(fromBytes->deckList.writeToString_Native(), fromFile->deckList.writeToString_Native());
        EXPECT_EQ(fromBytes->deckList.getDeckHash(), fromFile->deckList.getDeckHash());
        EXPECT_EQ(fromBytes->lastLoadInfo.fileFormat, fromFile->lastLoadInfo.fileFormat);
    }
};

TEST_F(DeckBytes, ACockatriceDeckRoundTripsThroughBytes)
{
    const QByteArray bytes = DeckLoader::toBytes(burn(), DeckFileFormat::Cockatrice);
    EXPECT_EQ(bytes, written(burn(), "Burn.cod", DeckFileFormat::Cockatrice));

    const auto loaded = DeckLoader::loadFromBytes("Burn.cod", bytes);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->lastLoadInfo.fileFormat, DeckFileFormat::Cockatrice);
    EXPECT_EQ(loaded->deckList.getName(), "Burn");
    EXPECT_EQ(loaded->deckList.writeToString_Native(), burn().writeToString_Native());
    expectSameAsFromFile("Burn.cod", bytes);
}

TEST_F(DeckBytes, APlainTextDeckRoundTripsThroughBytes)
{
    const QByteArray bytes = DeckLoader::toBytes(burn(), DeckFileFormat::PlainText);
    EXPECT_EQ(bytes, written(burn(), "Burn.txt", DeckFileFormat::PlainText));

    const auto loaded = DeckLoader::loadFromBytes("Burn.txt", bytes);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->lastLoadInfo.fileFormat, DeckFileFormat::PlainText);
    EXPECT_EQ(loaded->deckList.getCardList(), burn().getCardList());
    EXPECT_EQ(DeckLoader::toBytes(loaded->deckList, DeckFileFormat::PlainText), bytes);
    expectSameAsFromFile("Burn.txt", bytes);
}

TEST_F(DeckBytes, TheFormatComesFromTheNameAsForAFile)
{
    const QByteArray plain = DeckLoader::toBytes(burn(), DeckFileFormat::PlainText);

    const auto namedCod = DeckLoader::loadFromBytes("Burn.COD", plain);
    ASSERT_TRUE(namedCod.has_value());
    EXPECT_EQ(namedCod->lastLoadInfo.fileFormat, DeckFileFormat::PlainText);
    expectSameAsFromFile("Burn.COD", plain);

    const auto namedDek = DeckLoader::loadFromBytes("Burn.dek", plain);
    ASSERT_TRUE(namedDek.has_value());
    EXPECT_EQ(namedDek->lastLoadInfo.fileFormat, DeckFileFormat::PlainText);
}

TEST_F(DeckBytes, BytesThatAreNotADeckGiveNoValue)
{
    for (const char *name : {"Burn.cod", "Burn.txt"}) {
        EXPECT_FALSE(DeckLoader::loadFromBytes(name, QByteArray()).has_value()) << name;
        EXPECT_FALSE(DeckLoader::loadFromBytes(name, "%%% ### ***\n--- !!!\n").has_value()) << name;
    }
}
} // namespace

int main(int argc, char **argv)
{
    return DialogTest::run(argc, argv, QStringLiteral("deck-loader-bytes-test"), [] {
        const int result = RUN_ALL_TESTS();

        QDir(SettingsCache::instance().getDataPath()).removeRecursively();
        QDir(SettingsCache::instance().getCachePath()).removeRecursively();
        return result;
    });
}

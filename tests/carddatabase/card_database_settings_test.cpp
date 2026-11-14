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

#include "libcockatrice/card/database/card_database.h"
#include "libcockatrice/card/database/card_database_querier.h"
#include "libcockatrice/card/printing/printing_info.h"
#include "libcockatrice/card/set/card_set.h"
#include "libcockatrice/card/set/card_set_list.h"

#include "gtest/gtest.h"
#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>
#include <libcockatrice/interfaces/interface_card_database_path_provider.h>
#include <libcockatrice/interfaces/interface_card_set_priority_controller.h>
#include <libcockatrice/interfaces/noop_card_preference_provider.h>
#include <libcockatrice/settings/card_database_settings.h>

namespace
{

using SetOptions = ICardSetPriorityController::SetOptions;
using SetSaveData = ICardSetPriorityController::SetSaveData;

class CountingSettings : public CardDatabaseSettings
{
public:
    using CardDatabaseSettings::CardDatabaseSettings;

    void saveSets(const QVector<SetSaveData> &data) override
    {
        ++batchedWrites;
        for (const SetSaveData &entry : data) {
            savedNames << entry.shortName;
        }
        CardDatabaseSettings::saveSets(data);
    }

    int batchedWrites = 0;
    QStringList savedNames;
};

class DirPathProvider : public ICardDatabasePathProvider
{
public:
    explicit DirPathProvider(const QString &_root) : root(_root)
    {
    }

    QString getCardDatabasePath() const override
    {
        return root + "/cards.xml";
    }

    QString getCustomCardDatabasePath() const override
    {
        return root + "/customsets/";
    }

    QString getTokenDatabasePath() const override
    {
        return root + "/tokens.xml";
    }

    QString getSpoilerCardDatabasePath() const override
    {
        return root + "/spoiler.xml";
    }

private:
    QString root;
};

class CardDatabaseSettingsUnsavedSetsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(tempDir.isValid());
    }

    QTemporaryDir tempDir;
};

TEST_F(CardDatabaseSettingsUnsavedSetsTest, SwitchOffReadsUnsavedSetDisabled)
{
    CardDatabaseSettings settings(tempDir.path() + "/");

    const SetOptions options = settings.getSetOptions("NEW");
    EXPECT_FALSE(options.enabled);
    EXPECT_FALSE(options.isKnown);
    EXPECT_EQ(options.sortKey, 0u);
    EXPECT_FALSE(settings.isEnabled("NEW"));
}

TEST_F(CardDatabaseSettingsUnsavedSetsTest, SwitchOnReadsUnsavedSetEnabledAndUnknown)
{
    CardDatabaseSettings settings(tempDir.path() + "/");
    settings.setUnsavedSetsEnabled(true);

    const SetOptions options = settings.getSetOptions("NEW");
    EXPECT_TRUE(options.enabled);
    EXPECT_FALSE(options.isKnown);
    EXPECT_EQ(options.sortKey, 0u);
    EXPECT_TRUE(settings.isEnabled("NEW"));
    EXPECT_FALSE(settings.isKnown("NEW"));
    EXPECT_EQ(settings.getSortKey("NEW"), 0u);
    EXPECT_TRUE(settings.getEnabledSetNames().isEmpty()) << "an unsaved set is not a saved enabled set";
}

TEST_F(CardDatabaseSettingsUnsavedSetsTest, SwitchOnKeepsSavedDisabledSet)
{
    {
        CardDatabaseSettings writer(tempDir.path() + "/");
        writer.saveSets({{"OLD", 4, false, true}});
    }

    CardDatabaseSettings settings(tempDir.path() + "/");
    settings.setUnsavedSetsEnabled(true);

    const SetOptions options = settings.getSetOptions("OLD");
    EXPECT_FALSE(options.enabled) << "a saved value wins over the default";
    EXPECT_TRUE(options.isKnown);
    EXPECT_EQ(options.sortKey, 4u);
    EXPECT_FALSE(settings.isEnabled("OLD"));
}

// The checked-in fixtures declare no set metadata, so every one of their sets counts as ignored and could
// never become known.
class WebSetAdoptionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(tempDir.isValid());
        root = tempDir.path();
        QFile cards(root + "/cards.xml");
        ASSERT_TRUE(cards.open(QIODevice::WriteOnly));
        cards.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<cockatrice_carddatabase version=\"4\">\n"
                    "  <sets>\n"
                    "    <set><name>AAA</name><longname>Alpha</longname><settype>core</settype>"
                    "<releasedate>2020-01-01</releasedate><priority>10</priority></set>\n"
                    "    <set><name>BBB</name><longname>Beta</longname><settype>expansion</settype>"
                    "<releasedate>2022-06-01</releasedate><priority>10</priority></set>\n"
                    "    <set><name>CCC</name><longname>Gamma</longname><settype>promo</settype>"
                    "<releasedate>2023-01-01</releasedate><priority>20</priority></set>\n"
                    "  </sets>\n"
                    "  <cards>\n"
                    "    <card><name>Bolt</name><set>AAA</set><set>BBB</set><set>CCC</set>"
                    "<tablerow>3</tablerow><text>Deal 3 damage.</text></card>\n"
                    "    <card><name>Ghost</name><set>XXX</set><tablerow>1</tablerow><text>Boo.</text></card>\n"
                    "  </cards>\n"
                    "</cockatrice_carddatabase>\n");
        cards.close();
    }

    QString settingsDir(const QString &name) const
    {
        return root + "/" + name + "-";
    }

    QHash<QString, int> enableAllBatchedKeys() const
    {
        CardDatabaseSettings settings(settingsDir("reference"));
        settings.setUnsavedSetsEnabled(true);
        NoopCardPreferenceProvider prefs;
        DirPathProvider paths(root);
        CardDatabase db(nullptr, &prefs, &paths, &settings);
        db.loadCardDatabases();
        CardSetList sets = db.getSetList();
        sets.enableAllBatched(&settings);
        QHash<QString, int> keys;
        for (const CardSetPtr &set : sets) {
            keys.insert(set->getShortName(), set->getSortKey());
        }
        return keys;
    }

    static QStringList printingSets(const CardDatabase &db, const QString &cardName)
    {
        const CardInfoPtr card = db.getCardList().value(cardName);
        return card ? card->getSets().keys() : QStringList{"<no card>"};
    }

    QTemporaryDir tempDir;
    QString root;
};

TEST_F(WebSetAdoptionTest, LoadWithSwitchOnKeepsPrintingsOfUnsavedSets)
{
    CardDatabaseSettings settings(settingsDir("profile"));
    settings.setUnsavedSetsEnabled(true);
    NoopCardPreferenceProvider prefs;
    DirPathProvider paths(root);
    CardDatabase db(nullptr, &prefs, &paths, &settings);

    db.loadCardDatabases();

    EXPECT_EQ(printingSets(db, "Bolt"), QStringList({"AAA", "BBB", "CCC"}));
    EXPECT_EQ(printingSets(db, "Ghost"), QStringList({"XXX"}));
}

TEST_F(WebSetAdoptionTest, FirstVisitAdoptionMatchesEnableAllWithoutReparse)
{
    CountingSettings settings(settingsDir("profile"));
    settings.setUnsavedSetsEnabled(true);
    NoopCardPreferenceProvider prefs;
    DirPathProvider paths(root);
    CardDatabase db(nullptr, &prefs, &paths, &settings);
    db.loadCardDatabases();
    const CardInfoPtr bolt = db.getCardList().value("Bolt");
    ASSERT_FALSE(bolt.isNull());
    QSignalSpy resets(&db, &CardDatabase::cardDatabaseReset);
    QSignalSpy loads(&db, &CardDatabase::cardDatabaseLoadingFinished);
    QSignalSpy allEnabled(&db, &CardDatabase::cardDatabaseAllNewSetsEnabled);
    QSignalSpy newSets(&db, &CardDatabase::cardDatabaseNewSetsFound);

    db.adoptLoadedSets();

    const QHash<QString, int> expectedKeys = enableAllBatchedKeys();
    ASSERT_EQ(expectedKeys.size(), 4);
    CardDatabaseSettings reread(settingsDir("profile"));
    for (const CardSetPtr &set : db.getSetList()) {
        const QString name = set->getShortName();
        const std::string label = name.toStdString();
        EXPECT_EQ(set->getSortKey(), expectedKeys.value(name)) << label;
        EXPECT_TRUE(set->getEnabled()) << label;
        EXPECT_EQ(set->getIsKnown(), !set->getIsKnownIgnored()) << label;
        const SetOptions saved = reread.getSetOptions(name);
        EXPECT_EQ(static_cast<int>(saved.sortKey), expectedKeys.value(name)) << label;
        EXPECT_TRUE(saved.enabled) << label;
        EXPECT_EQ(saved.isKnown, !set->getIsKnownIgnored()) << label;
    }
    EXPECT_EQ(settings.batchedWrites, 1);

    EXPECT_EQ(resets.count(), 0);
    EXPECT_EQ(loads.count(), 0);
    EXPECT_EQ(allEnabled.count(), 0);
    EXPECT_EQ(newSets.count(), 0);
    EXPECT_EQ(db.getCardList().value("Bolt"), bolt) << "the parsed cards stay in place";
    const CardSetPtr preferredSet = db.query()->getPreferredPrinting("Bolt").getSet();
    ASSERT_FALSE(preferredSet.isNull());
    EXPECT_EQ(preferredSet->getShortName(), "BBB") << "the newest primary set comes first";
}

TEST_F(WebSetAdoptionTest, AdoptionWithSavedSetsAdoptsOnlyTheNewOnes)
{
    CountingSettings settings(settingsDir("profile"));
    settings.saveSets({{"AAA", 7, true, true}, {"BBB", 3, true, true}});
    settings.batchedWrites = 0;
    settings.savedNames.clear();
    settings.setUnsavedSetsEnabled(true);
    NoopCardPreferenceProvider prefs;
    DirPathProvider paths(root);
    CardDatabase db(nullptr, &prefs, &paths, &settings);
    db.loadCardDatabases();
    ASSERT_EQ(printingSets(db, "Bolt"), QStringList({"AAA", "BBB", "CCC"}));
    QSignalSpy resets(&db, &CardDatabase::cardDatabaseReset);
    QSignalSpy newSets(&db, &CardDatabase::cardDatabaseNewSetsFound);

    db.adoptLoadedSets();

    const CardSetPtr aaa = db.getSet("AAA");
    const CardSetPtr bbb = db.getSet("BBB");
    const CardSetPtr ccc = db.getSet("CCC");
    const CardSetPtr xxx = db.getSet("XXX");
    EXPECT_EQ(aaa->getSortKey(), 7);
    EXPECT_EQ(bbb->getSortKey(), 3);
    EXPECT_TRUE(aaa->getEnabled() && aaa->getIsKnown());
    EXPECT_TRUE(bbb->getEnabled() && bbb->getIsKnown());
    EXPECT_TRUE(ccc->getEnabled());
    EXPECT_TRUE(ccc->getIsKnown());
    EXPECT_EQ(ccc->getSortKey(), 0) << "a new set keeps the key enableAllUnknownBatched() leaves it";
    EXPECT_TRUE(xxx->getEnabled());
    EXPECT_FALSE(xxx->getIsKnown()) << "a set without metadata never becomes known";

    EXPECT_EQ(settings.batchedWrites, 1);
    EXPECT_EQ(settings.savedNames, QStringList{"CCC"});
    CardDatabaseSettings reread(settingsDir("profile"));
    EXPECT_TRUE(reread.getSetOptions("CCC").enabled);
    EXPECT_TRUE(reread.getSetOptions("CCC").isKnown);
    EXPECT_EQ(reread.getSetOptions("AAA").sortKey, 7u);
    EXPECT_EQ(reread.getSetOptions("BBB").sortKey, 3u);

    EXPECT_EQ(resets.count(), 0);
    EXPECT_EQ(newSets.count(), 0);
    EXPECT_EQ(printingSets(db, "Bolt"), QStringList({"AAA", "BBB", "CCC"}));
}

TEST_F(WebSetAdoptionTest, PlayerDisabledSetsStayDisabledAndDropped)
{
    CardDatabaseSettings settings(settingsDir("profile"));
    settings.saveSets({{"AAA", 0, false, true}, {"BBB", 1, true, true}, {"XXX", 2, false, false}});
    settings.setUnsavedSetsEnabled(true);
    NoopCardPreferenceProvider prefs;
    DirPathProvider paths(root);
    CardDatabase db(nullptr, &prefs, &paths, &settings);
    db.loadCardDatabases();
    ASSERT_EQ(printingSets(db, "Bolt"), QStringList({"BBB", "CCC"}));
    ASSERT_EQ(printingSets(db, "Ghost"), QStringList());

    db.adoptLoadedSets();

    EXPECT_FALSE(db.getSet("AAA")->getEnabled());
    EXPECT_FALSE(db.getSet("XXX")->getEnabled()) << "a disabled set without metadata stays disabled too";
    EXPECT_TRUE(db.getSet("CCC")->getEnabled() && db.getSet("CCC")->getIsKnown());
    EXPECT_EQ(db.getSet("AAA")->getSortKey(), 0);
    EXPECT_EQ(db.getSet("BBB")->getSortKey(), 1);
    CardDatabaseSettings reread(settingsDir("profile"));
    EXPECT_FALSE(reread.getSetOptions("AAA").enabled);
    EXPECT_FALSE(reread.getSetOptions("XXX").enabled);
    EXPECT_EQ(printingSets(db, "Bolt"), QStringList({"BBB", "CCC"}));
    EXPECT_EQ(printingSets(db, "Ghost"), QStringList());
}

TEST_F(WebSetAdoptionTest, KnownIgnoredSetDoesNotHideAFirstVisit)
{
    CardDatabaseSettings settings(settingsDir("profile"));
    settings.saveSets({{"XXX", 0, true, true}});
    settings.setUnsavedSetsEnabled(true);
    NoopCardPreferenceProvider prefs;
    DirPathProvider paths(root);
    CardDatabase db(nullptr, &prefs, &paths, &settings);
    db.loadCardDatabases();

    db.adoptLoadedSets();

    const QHash<QString, int> expectedKeys = enableAllBatchedKeys();
    for (const CardSetPtr &set : db.getSetList()) {
        EXPECT_EQ(set->getSortKey(), expectedKeys.value(set->getShortName())) << set->getShortName().toStdString();
        EXPECT_TRUE(set->getIsKnown()) << set->getShortName().toStdString();
    }
}

TEST_F(WebSetAdoptionTest, AdoptionKeepsTheOrderAnOverlappingLoadSaved)
{
    CardDatabaseSettings settings(settingsDir("profile"));
    settings.setUnsavedSetsEnabled(true);
    NoopCardPreferenceProvider prefs;
    DirPathProvider paths(root);
    CardDatabase later(nullptr, &prefs, &paths, &settings);
    later.loadCardDatabases();
    CardDatabase first(nullptr, &prefs, &paths, &settings);
    first.loadCardDatabases();
    first.adoptLoadedSets();
    // A set the later parse created only after the first adoption had saved the order.
    later.getSet("CCC")->loadSetOptions();
    // The player disables a set in between.
    settings.saveSets({{"AAA", static_cast<unsigned int>(first.getSet("AAA")->getSortKey()), false, true}});

    later.adoptLoadedSets();

    EXPECT_FALSE(CardDatabaseSettings(settingsDir("profile")).getSetOptions("AAA").enabled);
    const QHash<QString, int> expectedKeys = enableAllBatchedKeys();
    CardDatabaseSettings reread(settingsDir("profile"));
    for (const CardSetPtr &set : later.getSetList()) {
        const QString name = set->getShortName();
        EXPECT_EQ(set->getSortKey(), expectedKeys.value(name)) << name.toStdString();
        EXPECT_EQ(static_cast<int>(reread.getSetOptions(name).sortKey), expectedKeys.value(name)) << name.toStdString();
    }
}

TEST_F(WebSetAdoptionTest, SetsWithoutMetadataKeepTheirSavedOrder)
{
    ASSERT_TRUE(QFile::remove(root + "/cards.xml"));
    ASSERT_TRUE(QFile::copy(QString(CARDDB_DATADIR) + "cards.xml", root + "/cards.xml"));
    const QHash<QString, int> customKeys = {{"WHO", 0}, {"DOG", 1}, {"CAT", 2}, {" Not a Card", 3}};
    CardDatabaseSettings settings(settingsDir("profile"));
    QVector<SetSaveData> custom;
    for (auto it = customKeys.cbegin(); it != customKeys.cend(); ++it) {
        custom.append({it.key(), static_cast<unsigned int>(it.value()), true, false});
    }
    settings.saveSets(custom);
    settings.setUnsavedSetsEnabled(true);
    NoopCardPreferenceProvider prefs;
    DirPathProvider paths(root);
    CardDatabase db(nullptr, &prefs, &paths, &settings);
    db.loadCardDatabases();
    ASSERT_EQ(db.getSetList().size(), customKeys.size());

    db.adoptLoadedSets();

    CardDatabaseSettings reread(settingsDir("profile"));
    for (const CardSetPtr &set : db.getSetList()) {
        const QString name = set->getShortName();
        ASSERT_TRUE(set->getIsKnownIgnored()) << name.toStdString();
        EXPECT_EQ(set->getSortKey(), customKeys.value(name)) << name.toStdString();
        EXPECT_EQ(static_cast<int>(reread.getSetOptions(name).sortKey), customKeys.value(name)) << name.toStdString();
    }
}

} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

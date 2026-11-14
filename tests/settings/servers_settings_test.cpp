#include "gtest/gtest.h"
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QVariant>
#include <libcockatrice/settings/servers_settings.h>

namespace
{
class ServersSettingsTest : public ::testing::Test
{
protected:
    QTemporaryDir tempDir;
    QString settingsPath;

    void SetUp() override
    {
        settingsPath = tempDir.path() + "/";
    }

    QString settingsFile() const
    {
        return settingsPath + "servers.ini";
    }

    void writeLegacyServer(int index,
                           const QString &saveName,
                           const QString &host,
                           const QString &port,
                           const QString &username,
                           const QString &password,
                           bool savePassword,
                           const QString &site)
    {
        QSettings settings(settingsFile(), QSettings::IniFormat);
        const QString prefix = QStringLiteral("server/server_details/");
        settings.setValue(prefix + QStringLiteral("saveName%1").arg(index), saveName);
        settings.setValue(prefix + QStringLiteral("server%1").arg(index), host);
        settings.setValue(prefix + QStringLiteral("port%1").arg(index), port);
        settings.setValue(prefix + QStringLiteral("username%1").arg(index), username);
        settings.setValue(prefix + QStringLiteral("password%1").arg(index), password);
        settings.setValue(prefix + QStringLiteral("savePassword%1").arg(index), savePassword);
        settings.setValue(prefix + QStringLiteral("site%1").arg(index), site);
    }
};

TEST_F(ServersSettingsTest, AddServerStoresAllFields)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", "secret", true, "https://alpha.example");

    const auto servers = settings.getServers();
    ASSERT_EQ(servers.size(), 1);
    EXPECT_EQ(servers.first().saveName, QString("Alpha"));
    EXPECT_EQ(servers.first().host, QString("alpha.example"));
    EXPECT_EQ(servers.first().port, QString("4748"));
    EXPECT_EQ(servers.first().username, QString("alice"));
    EXPECT_EQ(servers.first().password, QString("secret"));
    EXPECT_TRUE(servers.first().savePassword);
    EXPECT_EQ(servers.first().site, QString("https://alpha.example"));
}

TEST_F(ServersSettingsTest, AddExistingHostAndPortUpdatesInsteadOfDuplicating)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", "secret", true, "site-a");
    settings.addNewServer("Renamed", "alpha.example", "4748", "bob", "new-secret", true, "site-b");

    const auto servers = settings.getServers();
    ASSERT_EQ(servers.size(), 1);
    EXPECT_EQ(servers.first().saveName, QString("Renamed"));
    EXPECT_EQ(servers.first().username, QString("bob"));
    EXPECT_EQ(servers.first().password, QString("new-secret"));
    EXPECT_EQ(servers.first().site, QString("site-b"));
}

TEST_F(ServersSettingsTest, RemoveServerRemovesOnlyMatchingHostAndKeepsOrder)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", "a", true, "");
    settings.addNewServer("Beta", "beta.example", "4748", "bob", "b", true, "");
    settings.addNewServer("Gamma", "gamma.example", "4748", "carol", "c", true, "");

    settings.removeServer("beta.example");

    const auto servers = settings.getServers();
    ASSERT_EQ(servers.size(), 2);
    EXPECT_EQ(servers.at(0).saveName, QString("Alpha"));
    EXPECT_EQ(servers.at(1).saveName, QString("Gamma"));
}

TEST_F(ServersSettingsTest, RemovingLastServerDropsItsStaleArraySlot)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", "a", true, "");
    settings.addNewServer("Beta", "beta.example", "4748", "bob", "b", true, "");
    settings.addNewServer("Gamma", "gamma.example", "4748", "carol", "gamma-secret", true, "");

    settings.removeServer("gamma.example");

    QSettings raw(settingsFile(), QSettings::IniFormat);
    EXPECT_FALSE(raw.contains("server/server_details/servers/3/password"));
    EXPECT_FALSE(raw.contains("server/server_details/servers/3/saveName"));
    ASSERT_EQ(settings.getServers().size(), 2);
}

TEST_F(ServersSettingsTest, UpdateWithoutLossKeepsCredentials)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", "secret", true, "");

    EXPECT_TRUE(settings.updateExistingServerWithoutLoss("Renamed", "alpha.example", "1234", "site"));

    const auto server = settings.getServer(settings.findServerIndex("alpha.example", "1234"));
    ASSERT_TRUE(server.has_value());
    EXPECT_EQ(server->saveName, QString("Renamed"));
    EXPECT_EQ(server->username, QString("alice"));
    EXPECT_EQ(server->password, QString("secret"));
    EXPECT_TRUE(server->savePassword);
    EXPECT_EQ(server->site, QString("site"));
}

TEST_F(ServersSettingsTest, SetServerPasswordUpdatesOnlyTheNamedProfile)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", "old", true, "");
    settings.addNewServer("Beta", "beta.example", "4748", "bob", "keep", true, "");

    settings.setServerPassword("Beta", "new");

    EXPECT_EQ(settings.getServer(0)->password, QString("old"));
    EXPECT_EQ(settings.getServer(1)->password, QString("new"));
}

TEST_F(ServersSettingsTest, LegacyProfilesAreMigrated)
{
    {
        QSettings settings(settingsFile(), QSettings::IniFormat);
        settings.setValue("server/server_details/totalServers", 2);
    }
    writeLegacyServer(1, "Alpha", "alpha.example", "4748", "alice", "secret", true, "site-a");
    writeLegacyServer(2, "Beta", "beta.example", "4748", "bob", "other", false, "site-b");

    ServersSettings settings(settingsPath, nullptr);

    const auto servers = settings.getServers();
    ASSERT_EQ(servers.size(), 2);
    EXPECT_EQ(servers.at(0).saveName, QString("Alpha"));
    EXPECT_EQ(servers.at(0).host, QString("alpha.example"));
    EXPECT_TRUE(servers.at(0).savePassword);
    EXPECT_EQ(servers.at(1).saveName, QString("Beta"));
    EXPECT_FALSE(servers.at(1).savePassword);

    QSettings raw(settingsFile(), QSettings::IniFormat);
    EXPECT_FALSE(raw.contains("server/server_details/totalServers"));
    EXPECT_FALSE(raw.contains("server/server_details/saveName1"));
    EXPECT_FALSE(raw.contains("server/server_details/server1"));
}

TEST_F(ServersSettingsTest, MigrationRunsOnce)
{
    writeLegacyServer(1, "Alpha", "alpha.example", "4748", "alice", "secret", true, "");
    {
        QSettings settings(settingsFile(), QSettings::IniFormat);
        settings.setValue("server/server_details/totalServers", 1);
    }

    {
        ServersSettings first(settingsPath, nullptr);
        ASSERT_EQ(first.getServers().size(), 1);
    }

    ServersSettings second(settingsPath, nullptr);
    ASSERT_EQ(second.getServers().size(), 1);
}

TEST_F(ServersSettingsTest, LegacyProfileAddedAfterMigrationIsMerged)
{
    {
        ServersSettings first(settingsPath, nullptr);
        first.addNewServer("Alpha", "alpha.example", "4748", "alice", "secret", true, "");
    }

    // An older build writes its own profile back in the legacy format after the
    // array migration already happened.
    writeLegacyServer(1, "Beta", "beta.example", "4748", "bob", "other", true, "site-b");
    // A duplicate of a profile the array already holds must not be added twice.
    writeLegacyServer(2, "Alpha duplicate", "alpha.example", "4748", "eve", "dupe", true, "");
    {
        QSettings settings(settingsFile(), QSettings::IniFormat);
        settings.setValue("server/server_details/totalServers", 2);
    }

    ServersSettings settings(settingsPath, nullptr);

    const auto servers = settings.getServers();
    ASSERT_EQ(servers.size(), 2);
    EXPECT_EQ(servers.at(0).saveName, QString("Alpha"));
    EXPECT_EQ(servers.at(0).password, QString("secret"));
    EXPECT_EQ(servers.at(1).saveName, QString("Beta"));
    EXPECT_EQ(servers.at(1).host, QString("beta.example"));
    EXPECT_EQ(servers.at(1).username, QString("bob"));
    EXPECT_EQ(servers.at(1).password, QString("other"));
    EXPECT_TRUE(servers.at(1).savePassword);
    EXPECT_EQ(servers.at(1).site, QString("site-b"));

    QSettings raw(settingsFile(), QSettings::IniFormat);
    EXPECT_FALSE(raw.contains("server/server_details/totalServers"));
    EXPECT_FALSE(raw.contains("server/server_details/saveName1"));
    EXPECT_FALSE(raw.contains("server/server_details/server2"));

    // The merge is persisted, so a further start adds nothing.
    ServersSettings again(settingsPath, nullptr);
    ASSERT_EQ(again.getServers().size(), 2);
}

TEST_F(ServersSettingsTest, AddingAServerWithoutTheSaveFlagStoresNoPassword)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", "secret", false);

    QSettings raw(settingsFile(), QSettings::IniFormat);
    EXPECT_EQ(raw.value("server/server_details/servers/1/password").toString(), QString());
    EXPECT_EQ(raw.value("server/server_details/servers/1/username").toString(), QString("alice"));
}

TEST_F(ServersSettingsTest, UpdatingAServerWithoutTheSaveFlagStoresNoPasswordOrVerifier)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", QString(), false);

    EXPECT_TRUE(settings.updateExistingServer("Alpha", "alpha.example", "4748", "alice", "secret", false));
    settings.setServerPassword("Alpha", "$scrypt$made-up-verifier");

    QSettings raw(settingsFile(), QSettings::IniFormat);
    EXPECT_EQ(raw.value("server/server_details/servers/1/password").toString(), QString());
}

TEST_F(ServersSettingsTest, OutOfRangeIndexReturnsNullopt)
{
    ServersSettings settings(settingsPath, nullptr);
    EXPECT_FALSE(settings.getServer(-1).has_value());
    EXPECT_FALSE(settings.getServer(0).has_value());
}

class PasswordStorageTest : public ServersSettingsTest, public ::testing::WithParamInterface<bool>
{
protected:
    bool previous = ServersSettings::canStorePasswords();

    void SetUp() override
    {
        ServersSettingsTest::SetUp();
        ServersSettings::setPasswordStorageForTesting(GetParam());
    }

    void TearDown() override
    {
        ServersSettings::setPasswordStorageForTesting(previous);
    }

    QVariant storedField(const QString &field) const
    {
        return QSettings(settingsFile(), QSettings::IniFormat).value("server/server_details/servers/1/" + field);
    }

    QString allowed(const QString &password) const
    {
        return GetParam() ? password : QString();
    }

    void writeSavedLogin() const
    {
        QSettings file(settingsFile(), QSettings::IniFormat);
        file.setValue("server/previousHostName", "Alpha");
        file.setValue("server/autoConnect", 1);
        file.beginGroup("server/server_details");
        file.beginWriteArray("servers", 1);
        file.setArrayIndex(0);
        file.setValue("saveName", "Alpha");
        file.setValue("server", "alpha.example");
        file.setValue("port", "4748");
        file.setValue("username", "alice");
        file.setValue("password", "secret");
        file.setValue("savePassword", true);
        file.endArray();
        file.endGroup();
    }
};

TEST_P(PasswordStorageTest, AddingAServerStoresItsPasswordOnlyWhereAllowed)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", "secret", true);

    EXPECT_EQ(storedField("password").toString(), allowed("secret"));
    EXPECT_EQ(storedField("savePassword").toBool(), GetParam());
    EXPECT_EQ(storedField("username").toString(), QString("alice"));
}

TEST_P(PasswordStorageTest, UpdatingAServerStoresItsPasswordOnlyWhereAllowed)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", QString(), false);

    EXPECT_TRUE(settings.updateExistingServer("Alpha", "alpha.example", "4748", "alice", "secret", true));

    EXPECT_EQ(storedField("password").toString(), allowed("secret"));
    EXPECT_EQ(storedField("savePassword").toBool(), GetParam());
}

TEST_P(PasswordStorageTest, AVerifierIsStoredOnlyWhereAllowed)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", QString(), true);

    settings.setServerPassword("Alpha", "$scrypt$made-up-verifier");

    EXPECT_EQ(storedField("password").toString(), allowed("$scrypt$made-up-verifier"));
}

TEST_P(PasswordStorageTest, StoredCredentialsAreReportedOnlyWhereAllowed)
{
    ServersSettings settings(settingsPath, nullptr);
    settings.addNewServer("Alpha", "alpha.example", "4748", "alice", "secret", true);
    settings.setPrevioushostName("Alpha");

    EXPECT_EQ(settings.hasCredentials("alpha.example", "4748"), GetParam());
    EXPECT_EQ(settings.hasLoginData("alpha.example", "4748"), GetParam());
    EXPECT_EQ(settings.getSavePassword(), GetParam());
    EXPECT_EQ(settings.getPassword(), allowed("secret"));
    EXPECT_EQ(settings.getServers().value(0).password, allowed("secret"));
}

TEST_P(PasswordStorageTest, APasswordAlreadyOnDiskIsReadAndKeptOnlyWhereAllowed)
{
    writeSavedLogin();

    ServersSettings settings(settingsPath, nullptr);

    EXPECT_EQ(settings.getPassword(), allowed("secret"));
    EXPECT_EQ(settings.getSavePassword(), GetParam());
    EXPECT_EQ(settings.hasLoginData("alpha.example", "4748"), GetParam());
    EXPECT_EQ(settings.getServers().value(0).password, allowed("secret"));
    EXPECT_EQ(settings.getServers().value(0).username, QString("alice"));
    EXPECT_EQ(storedField("password").toString(), allowed("secret"));
    EXPECT_EQ(storedField("username").toString(), QString("alice"));
}

TEST_P(PasswordStorageTest, AStoredAutoConnectCountsOnlyWhereAPasswordCanBeStored)
{
    writeSavedLogin();

    ServersSettings settings(settingsPath, nullptr);

    EXPECT_EQ(settings.getAutoConnect(), GetParam() ? 1 : 0);
}

TEST_P(PasswordStorageTest, AMigratedLegacyPasswordIsKeptOnlyWhereAllowed)
{
    {
        QSettings settings(settingsFile(), QSettings::IniFormat);
        settings.setValue("server/server_details/totalServers", 1);
    }
    writeLegacyServer(1, "Alpha", "alpha.example", "4748", "alice", "secret", true, "");

    ServersSettings settings(settingsPath, nullptr);

    EXPECT_EQ(storedField("password").toString(), allowed("secret"));
    EXPECT_EQ(storedField("savePassword").toBool(), GetParam());
    EXPECT_FALSE(QSettings(settingsFile(), QSettings::IniFormat).contains("server/server_details/password1"));
}

INSTANTIATE_TEST_SUITE_P(Storage,
                         PasswordStorageTest,
                         ::testing::Bool(),
                         [](const ::testing::TestParamInfo<bool> &info) {
                             return info.param ? "Allowed" : "NotAllowed";
                         });
} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

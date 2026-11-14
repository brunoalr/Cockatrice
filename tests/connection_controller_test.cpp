#include "client/network/connection_controller/remote_connection_controller.h"
#include "client/settings/cache_settings.h"
#include "dialog_test_support.h"
#include "interface/intents/contexts/context_connect_to_server.h"
#include "interface/intents/intent_login.h"
#include "interface/theme_manager.h"
#include "interface/widgets/dialogs/dlg_connect.h"
#include "interface/widgets/dialogs/dlg_edit_password.h"
#include "interface/widgets/dialogs/dlg_forgot_password_request.h"
#include "interface/widgets/dialogs/dlg_forgot_password_reset.h"
#include "interface/widgets/dialogs/dlg_login_prompt.h"
#include "interface/widgets/dialogs/dlg_register.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>
#include <libcockatrice/network/client/remote/remote_client.h>
#include <libcockatrice/protocol/pb/response.pb.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <libcockatrice/settings/servers_settings.h>
#include <libcockatrice/utility/string_limits.h>
#include <memory>
#include <optional>

ThemeManager *themeManager = nullptr; // NOSONAR: the client declares it extern and assigns it
RNG_Abstract *rng = nullptr;          // NOSONAR: the client declares it extern and assigns it

namespace
{
using DialogTest::settle;

class ConnectionControllerDialogs : public DialogTest::Fixture // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    std::unique_ptr<ConnectionController> controller;
    RemoteClient *client = nullptr;

    void SetUp() override
    {
        controller = std::make_unique<ConnectionController>(window.get());
        client = controller->client();
        // Stopped, the client thread leaves every request queued: the tests only see the request signals.
        client->thread()->quit();
        client->thread()->wait();
    }

    static DlgLoginPrompt *openLoginPrompt()
    {
        QList<DlgLoginPrompt *> open;
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *prompt = qobject_cast<DlgLoginPrompt *>(widget); prompt && prompt->isVisible()) {
                open.append(prompt);
            }
        }
        EXPECT_EQ(open.size(), 1);
        return open.value(0);
    }

    void clickOk() const
    {
        QMessageBox *box = onlyOpen<QMessageBox>();
        ASSERT_NE(box, nullptr);
        box->button(QMessageBox::Ok)->click();
        settle();
    }
};

TEST_F(ConnectionControllerDialogs, ConnectReturnsWithTheDialogOpen)
{
    controller->connectToServer();

    DlgConnect *dialog = onlyOpen<DlgConnect>();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->windowModality(), Qt::ApplicationModal);
}

TEST_F(ConnectionControllerDialogs, AcceptingConnectConnectsWithTheTypedLogin)
{
    std::optional<QStringList> request;
    QObject::connect(client, &RemoteClient::sigConnectToServer,
                     [&request](const QString &host, unsigned int port, const QString &user, const QString &password) {
                         request = QStringList{host, QString::number(port), user, password};
                     });
    controller->connectToServer();
    DlgConnect *dialog = onlyOpen<DlgConnect>();
    ASSERT_NE(dialog, nullptr);
    dialog->findChild<QLineEdit *>("connectHostEdit")->setText("typed.example");
    dialog->findChild<QLineEdit *>("connectPortEdit")->setText("4748");
    dialog->findChild<QLineEdit *>("connectPlayernameEdit")->setText("typed player");
    dialog->findChild<QLineEdit *>("connectPasswordEdit")->setText("secret");

    dialog->accept();
    settle();

    EXPECT_EQ(request, (QStringList{"typed.example", "4748", "typed player", "secret"}));
    EXPECT_TRUE(openDialogs<DlgConnect>().isEmpty());
}

TEST_F(ConnectionControllerDialogs, RejectingConnectConnectsNowhere)
{
    bool requested = false;
    QObject::connect(client, &RemoteClient::sigConnectToServer, [&requested] { requested = true; });
    controller->connectToServer();
    DlgConnect *dialog = onlyOpen<DlgConnect>();
    ASSERT_NE(dialog, nullptr);

    dialog->reject();
    settle();

    EXPECT_FALSE(requested);
}

TEST_F(ConnectionControllerDialogs, ASecondConnectKeepsOneDialog)
{
    controller->connectToServer();
    controller->connectToServer();

    EXPECT_EQ(openDialogs<DlgConnect>().size(), 1);
}

TEST_F(ConnectionControllerDialogs, ALoginErrorReopensConnectAfterItsNotice)
{
    emit client->loginError(Response::RespWrongPassword, QString(), 0, {});

    EXPECT_TRUE(openDialogs<DlgConnect>().isEmpty());
    clickOk();

    EXPECT_EQ(openDialogs<DlgConnect>().size(), 1);
    EXPECT_TRUE(openDialogs<QMessageBox>().isEmpty());
}

TEST_F(ConnectionControllerDialogs, AMissingFeatureLoginErrorListsThemBeforeReopeningConnect)
{
    emit client->loginError(Response::RespClientUpdateRequired, QString(), 0, {"Feature"});

    QMessageBox *box = onlyOpen<QMessageBox>();
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->icon(), QMessageBox::Critical);
    EXPECT_TRUE(box->detailedText().contains("Feature"));
    EXPECT_TRUE(openDialogs<DlgConnect>().isEmpty());
    box->close();
    settle();

    EXPECT_EQ(openDialogs<DlgConnect>().size(), 1);
}

TEST_F(ConnectionControllerDialogs, ASocketErrorReopensConnectAfterItsNotice)
{
    emit client->socketError("refused");

    EXPECT_TRUE(openDialogs<DlgConnect>().isEmpty());
    clickOk();

    EXPECT_EQ(openDialogs<DlgConnect>().size(), 1);
}

TEST_F(ConnectionControllerDialogs, RegistrationRequiredRegistersOnlyOnYes)
{
    emit client->loginError(Response::RespRegistrationRequired, QString(), 0, {});
    QMessageBox *box = onlyOpen<QMessageBox>();
    ASSERT_NE(box, nullptr);
    box->button(QMessageBox::No)->click();
    settle();

    EXPECT_TRUE(openDialogs<DlgRegister>().isEmpty());
    EXPECT_TRUE(openDialogs<DlgConnect>().isEmpty());

    emit client->loginError(Response::RespRegistrationRequired, QString(), 0, {});
    box = onlyOpen<QMessageBox>();
    ASSERT_NE(box, nullptr);
    box->button(QMessageBox::Yes)->click();
    settle();

    EXPECT_EQ(openDialogs<DlgRegister>().size(), 1);
    EXPECT_TRUE(openDialogs<DlgConnect>().isEmpty());
}

TEST_F(ConnectionControllerDialogs, AcceptingRegisterRegistersWithTheTypedAccount)
{
    std::optional<QStringList> request;
    QObject::connect(client, &RemoteClient::sigRegisterToServer,
                     [&request](const QString &host, unsigned int port, const QString &user, const QString &password,
                                const QString &email, const QString &country, const QString &realName) {
                         request = QStringList{host, QString::number(port), user, password, email, country, realName};
                     });
    controller->registerToServer();
    DlgRegister *dialog = onlyOpen<DlgRegister>();
    ASSERT_NE(dialog, nullptr);
    const QStringList typed{dialog->getHost(),       QString::number(dialog->getPort()),
                            dialog->getPlayerName(), dialog->getPassword(),
                            dialog->getEmail(),      dialog->getCountry(),
                            dialog->getRealName()};

    dialog->accept();
    settle();

    EXPECT_EQ(request, typed);
}

TEST_F(ConnectionControllerDialogs, ARegisterErrorReopensRegisterAfterItsNotice)
{
    emit client->registerError(Response::RespPasswordTooShort, QString(), 0);

    EXPECT_TRUE(openDialogs<DlgRegister>().isEmpty());
    clickOk();

    EXPECT_EQ(openDialogs<DlgRegister>().size(), 1);
}

TEST_F(ConnectionControllerDialogs, AnInactiveAccountSendsTheTypedToken)
{
    std::optional<QString> token;
    QObject::connect(client, &RemoteClient::sigActivateToServer, [&token](const QString &sent) { token = sent; });
    emit client->loginError(Response::RespAccountNotActivated, QString(), 0, {});

    QInputDialog *dialog = onlyOpen<QInputDialog>();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->findChild<QLineEdit *>()->maxLength(), MAX_NAME_LENGTH);
    dialog->setTextValue("token");
    dialog->accept();
    settle();

    EXPECT_EQ(token, "token");
    EXPECT_TRUE(openDialogs<DlgConnect>().isEmpty());
}

TEST_F(ConnectionControllerDialogs, CancellingTheActivationTokenDisconnects)
{
    bool disconnected = false;
    bool activated = false;
    QObject::connect(client, &RemoteClient::sigDisconnectFromServer, [&disconnected] { disconnected = true; });
    QObject::connect(client, &RemoteClient::sigActivateToServer, [&activated] { activated = true; });
    emit client->loginError(Response::RespAccountNotActivated, QString(), 0, {});

    QInputDialog *dialog = onlyOpen<QInputDialog>();
    ASSERT_NE(dialog, nullptr);
    dialog->setTextValue("token");
    dialog->reject();
    settle();

    EXPECT_TRUE(disconnected);
    EXPECT_FALSE(activated);
}

TEST_F(ConnectionControllerDialogs, ARequiredPasswordChangeSendsBothPasswords)
{
    std::optional<QStringList> change;
    QObject::connect(client, &RemoteClient::sigChangePasswordToServer,
                     [&change](const QString &oldPassword, const QString &newPassword) {
                         change = QStringList{oldPassword, newPassword};
                     });
    emit client->loginError(Response::RespPasswordChangeRequired, QString(), 0, {});

    DlgEditPassword *dialog = onlyOpen<DlgEditPassword>();
    ASSERT_NE(dialog, nullptr);
    const auto edits = dialog->findChildren<QLineEdit *>();
    for (int i = 0; i < edits.size(); ++i) {
        edits[i]->setText(QString("password %1").arg(i));
    }
    const QStringList typed{dialog->getOldPassword(), dialog->getNewPassword()};
    ASSERT_NE(typed[0], typed[1]);
    dialog->accept();
    settle();

    EXPECT_EQ(change, typed);
}

TEST_F(ConnectionControllerDialogs, ForgotPasswordFromConnectRequestsTheReset)
{
    std::optional<QString> user;
    QObject::connect(client, &RemoteClient::sigRequestForgotPasswordToServer,
                     [&user](const QString &, unsigned int, const QString &sent) { user = sent; });
    controller->connectToServer();
    DlgConnect *connectDialog = onlyOpen<DlgConnect>();
    ASSERT_NE(connectDialog, nullptr);

    emit connectDialog->sigStartForgotPasswordRequest();
    DlgForgotPasswordRequest *dialog = onlyOpen<DlgForgotPasswordRequest>();
    ASSERT_NE(dialog, nullptr);
    const QString typed = dialog->getPlayerName();
    dialog->accept();
    settle();

    EXPECT_EQ(user, typed);
    EXPECT_EQ(openDialogs<DlgConnect>().size(), 1);
}

TEST_F(ConnectionControllerDialogs, AResetPromptAsksForTheTokenAfterItsNotice)
{
    std::optional<QString> token;
    QObject::connect(client, &RemoteClient::sigSubmitForgotPasswordResetToServer,
                     [&token](const QString &, unsigned int, const QString &, const QString &sent, const QString &) {
                         token = sent;
                     });
    emit client->sigPromptForForgotPasswordReset();

    EXPECT_TRUE(openDialogs<DlgForgotPasswordReset>().isEmpty());
    clickOk();
    DlgForgotPasswordReset *dialog = onlyOpen<DlgForgotPasswordReset>();
    ASSERT_NE(dialog, nullptr);
    const QString typed = dialog->getToken();
    dialog->accept();
    settle();

    EXPECT_EQ(token, typed);
}

TEST_F(ConnectionControllerDialogs, AResetWithoutItsRequestWarnsOverTheOpenResetDialog)
{
    ServersSettings &servers = SettingsCache::instance().servers();
    const QStringList saved{servers.getFPHostname(), servers.getFPPort(), servers.getFPPlayerName()};
    servers.setFPHostName("");
    servers.setFPPort("");
    servers.setFPPlayerName("");

    emit client->sigPromptForForgotPasswordReset();
    clickOk();
    DlgForgotPasswordReset *dialog = onlyOpen<DlgForgotPasswordReset>();
    settle();

    servers.setFPHostName(saved[0]);
    servers.setFPPort(saved[1]);
    servers.setFPPlayerName(saved[2]);
    ASSERT_NE(dialog, nullptr);
    QMessageBox *warning = dialog->findChild<QMessageBox *>();
    ASSERT_NE(warning, nullptr);
    EXPECT_TRUE(warning->isVisible());
    EXPECT_EQ(warning->icon(), QMessageBox::Warning);
}

TEST_F(ConnectionControllerDialogs, ANoticeOutlivingTheControllerReopensNothing)
{
    emit client->socketError("refused");
    QMessageBox *box = onlyOpen<QMessageBox>();
    ASSERT_NE(box, nullptr);

    controller.reset();
    box->button(QMessageBox::Ok)->click();
    settle();

    EXPECT_TRUE(openDialogs<DlgConnect>().isEmpty());
}
TEST_F(ConnectionControllerDialogs, ALinkToAServerWithoutALoginAsksForOneAndSavesIt)
{
    ContextConnectToServer context{"unsaved.example", "4747", {}, {}};
    auto *intent = std::make_unique<IntentGetLoginCredentials>(&context, true).release();
    bool finished = false;
    QObject::connect(intent, &Intent::finished, [&finished] { finished = true; });
    intent->execute();

    DlgLoginPrompt *prompt = openLoginPrompt();
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->windowModality(), Qt::ApplicationModal);
    EXPECT_FALSE(finished);
    const auto edits = prompt->findChildren<QLineEdit *>();
    for (int i = 0; i < edits.size(); ++i) {
        edits[i]->setText(QString("field %1").arg(i));
    }
    prompt->findChild<QCheckBox *>()->setChecked(true);
    const QString user = prompt->username();
    const QString password = prompt->password();
    prompt->accept();
    settle();

    EXPECT_TRUE(finished);
    EXPECT_EQ(context.username, user);
    EXPECT_EQ(context.password, password);
    EXPECT_TRUE(SettingsCache::instance().servers().hasLoginData("unsaved.example", "4747"));
}

TEST_F(ConnectionControllerDialogs, CancellingTheLoginPromptCancelsTheLink)
{
    ContextConnectToServer context{"cancelled.example", "4747", {}, {}};
    auto *intent = std::make_unique<IntentGetLoginCredentials>(&context, true).release();
    bool cancelled = false;
    QObject::connect(intent, &Intent::cancelled, [&cancelled] { cancelled = true; });
    intent->execute();

    DlgLoginPrompt *prompt = openLoginPrompt();
    ASSERT_NE(prompt, nullptr);
    prompt->reject();
    settle();

    EXPECT_TRUE(cancelled);
    EXPECT_TRUE(context.username.isEmpty());
}

TEST(WebProfile, ACleanProfileListsOnlyRoosterRangesOnItsSecureWebSocketPort)
{
    QTemporaryDir profile;
    ASSERT_TRUE(profile.isValid());
    ServersSettings servers(profile.path() + "/");

    DlgConnect::saveWebServer(servers);

    const QList<ServersSettings::ServerEntry> saved = servers.getServers();
    ASSERT_EQ(saved.size(), 1);
    EXPECT_EQ(saved[0].saveName, "Rooster Ranges");
    EXPECT_EQ(saved[0].host, "server.cockatrice.us");
    EXPECT_EQ(saved[0].port, "443");
}
} // namespace

int main(int argc, char **argv)
{
    return DialogTest::run(argc, argv, QStringLiteral("connection-controller-test"), [] {
        ThemeManager theme;
        themeManager = &theme;
        ServersSettings &servers = SettingsCache::instance().servers();
        // With a single saved server the connect and register dialogs download the public server list.
        servers.addNewServer("First", "first.example", "4747", "first player", QString(), false);
        servers.addNewServer("Second", "second.example", "4747", "second player", QString(), false);
        servers.setFPHostName("first.example");
        servers.setFPPort("4747");
        servers.setFPPlayerName("first player");

        const int result = RUN_ALL_TESTS();

        QDir(SettingsCache::instance().getDataPath()).removeRecursively();
        QDir(SettingsCache::instance().getCachePath()).removeRecursively();
        return result;
    });
}

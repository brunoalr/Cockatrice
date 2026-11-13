#include "client/settings/cache_settings.h"
#include "dialog_test_support.h"
#include "interface/theme_manager.h"
#include "interface/widgets/dialogs/dlg_edit_avatar.h"
#include "interface/widgets/dialogs/dlg_report_user.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextEdit>
#include <gtest/gtest.h>
#include <libcockatrice/network/client/abstract/abstract_client.h>
#include <libcockatrice/protocol/pb/command_report.pb.h>
#include <libcockatrice/protocol/pb/commands.pb.h>
#include <libcockatrice/protocol/pb/response.pb.h>
#include <libcockatrice/protocol/pb/server_message.pb.h>
#include <libcockatrice/rng/rng_abstract.h>
#include <memory>

ThemeManager *themeManager = nullptr; // NOSONAR: the client declares it extern and assigns it
RNG_Abstract *rng = nullptr;          // NOSONAR: the client declares it extern and assigns it

namespace
{
using DialogTest::settle;

class RecordingClient : public AbstractClient
{
public:
    QList<CommandContainer> sent;

    ~RecordingClient() override
    {
        qDeleteAll(pendingCommands);
        pendingCommands.clear();
    }

    void answerLast(Response::ResponseCode code)
    {
        Response response;
        response.set_response_code(code);
        response.set_cmd_id(sent.last().cmd_id());
        ServerMessage message;
        message.set_message_type(ServerMessage::RESPONSE);
        *message.mutable_response() = response;
        processProtocolItem(message);
    }

protected:
    void sendCommandContainer(const CommandContainer &command) override
    {
        sent.append(command);
    }
};

class LobbyDialogs : public DialogTest::Fixture // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    RecordingClient client;

    // The dialogs hold the client: they go first.
    void TearDown() override
    {
        window.reset();
    }

    void clickInOnlyBox(QMessageBox::StandardButton button) const
    {
        const QMessageBox *box = onlyOpen<QMessageBox>();
        ASSERT_NE(box, nullptr);
        box->button(button)->click();
        settle();
    }

    DlgReportUser *openReport()
    {
        auto *dialog = new DlgReportUser(&client, "reported", 7, QString(), window.get());
        dialog->show();
        return dialog;
    }

    static void typeDescription(const DlgReportUser *dialog, const QString &text)
    {
        for (QTextEdit *edit : dialog->findChildren<QTextEdit *>()) {
            if (!edit->isReadOnly()) {
                edit->setPlainText(text);
            }
        }
    }

    static void submit(const DlgReportUser *dialog)
    {
        dialog->findChild<QDialogButtonBox *>(QString(), Qt::FindDirectChildrenOnly)
            ->button(QDialogButtonBox::Ok)
            ->click();
    }

    void browseForAvatar(const DlgEditAvatar *dialog, const QString &path) const
    {
        for (QPushButton *button : dialog->findChildren<QPushButton *>()) {
            if (button->text() == "Browse...") {
                button->click();
            }
        }
        QFileDialog *files = onlyOpen<QFileDialog>();
        ASSERT_NE(files, nullptr);
        EXPECT_EQ(files->windowModality(), Qt::ApplicationModal);
        if (path.isEmpty()) {
            files->reject();
        } else {
            files->selectFile(path);
            // QFileDialog::accept() is protected; through QDialog it still validates the chosen file.
            static_cast<QDialog *>(files)->accept();
        }
        settle();
    }
};

TEST_F(LobbyDialogs, AReportWithoutADescriptionWarnsAndSendsNothing)
{
    const DlgReportUser *dialog = openReport();

    submit(dialog);
    clickInOnlyBox(QMessageBox::Ok);

    EXPECT_TRUE(client.sent.isEmpty());
    EXPECT_TRUE(dialog->isVisible());
}

TEST_F(LobbyDialogs, AReportIsSentOnlyAfterYes)
{
    const DlgReportUser *dialog = openReport();
    typeDescription(dialog, "what happened");

    submit(dialog);
    clickInOnlyBox(QMessageBox::No);
    EXPECT_TRUE(client.sent.isEmpty());

    submit(dialog);
    clickInOnlyBox(QMessageBox::Yes);

    ASSERT_EQ(client.sent.size(), 1);
    const Command_Report &report = client.sent.first().session_command(0).GetExtension(Command_Report::ext);
    EXPECT_EQ(report.reported_user(), "reported");
    EXPECT_EQ(report.description(), "what happened");
    EXPECT_EQ(report.game_id(), 7);
}

TEST_F(LobbyDialogs, AnAcceptedReportClosesTheDialogAfterItsNotice)
{
    const DlgReportUser *dialog = openReport();
    typeDescription(dialog, "what happened");
    submit(dialog);
    clickInOnlyBox(QMessageBox::Yes);
    ASSERT_EQ(client.sent.size(), 1);

    client.answerLast(Response::RespOk);
    EXPECT_TRUE(dialog->isVisible());
    clickInOnlyBox(QMessageBox::Ok);

    EXPECT_FALSE(dialog->isVisible());
    EXPECT_EQ(dialog->result(), QDialog::Accepted);
}

TEST_F(LobbyDialogs, ARefusedReportKeepsTheDialogOpen)
{
    const DlgReportUser *dialog = openReport();
    typeDescription(dialog, "what happened");
    submit(dialog);
    clickInOnlyBox(QMessageBox::Yes);
    ASSERT_EQ(client.sent.size(), 1);

    client.answerLast(Response::RespTooManyRequests);
    clickInOnlyBox(QMessageBox::Ok);

    EXPECT_TRUE(dialog->isVisible());
}

TEST_F(LobbyDialogs, ChoosingAnAvatarImageLoadsIt)
{
    const QTemporaryDir dir;
    const QString path = dir.filePath("avatar.png");
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(path));
    auto *dialog = new DlgEditAvatar(window.get());
    dialog->show();

    browseForAvatar(dialog, path);

    EXPECT_FALSE(dialog->getImage().isEmpty());
    EXPECT_TRUE(openDialogs<QFileDialog>().isEmpty());
}

TEST_F(LobbyDialogs, CancellingTheAvatarFileDialogSaysNoImageWasChosen)
{
    const QTemporaryDir dir;
    const QString path = dir.filePath("not-an-image.png");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not an image");
    file.close();
    auto *dialog = new DlgEditAvatar(window.get());
    dialog->show();
    browseForAvatar(dialog, path);
    const QLabel *imageLabel = nullptr;
    for (const QLabel *label : dialog->findChildren<QLabel *>()) {
        if (label->text() == "Invalid image chosen.") {
            imageLabel = label;
        }
    }
    ASSERT_NE(imageLabel, nullptr);

    browseForAvatar(dialog, QString());

    EXPECT_EQ(imageLabel->text(), "No image chosen.");
}
} // namespace

int main(int argc, char **argv)
{
    return DialogTest::run(argc, argv, QStringLiteral("lobby-dialogs-test"), [] {
        ThemeManager theme;
        themeManager = &theme;

        const int result = RUN_ALL_TESTS();

        QDir(SettingsCache::instance().getDataPath()).removeRecursively();
        QDir(SettingsCache::instance().getCachePath()).removeRecursively();
        return result;
    });
}

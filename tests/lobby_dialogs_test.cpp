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
#include "interface/theme_manager.h"
#include "interface/widgets/dialogs/dlg_edit_avatar.h"
#include "interface/widgets/dialogs/dlg_report_user.h"

#include <QBuffer>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
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

    DialogTest::DevicePicker picker;

    void browseForAvatar(const DlgEditAvatar *dialog, const QString &fileName, const QByteArray &bytes)
    {
        for (QPushButton *button : dialog->findChildren<QPushButton *>()) {
            if (button->text() == "Browse...") {
                button->click();
            }
        }
        EXPECT_EQ(picker.filter, "Image Files (*.png *.jpg *.bmp)");
        EXPECT_TRUE(openDialogs<QFileDialog>().isEmpty());
        picker.pick(fileName, bytes);
    }

    static QByteArray png()
    {
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }

    static const QLabel *labelSaying(const DlgEditAvatar *dialog, const QString &text)
    {
        for (const QLabel *label : dialog->findChildren<QLabel *>()) {
            if (label->text() == text) {
                return label;
            }
        }
        return nullptr;
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
    auto *dialog = new DlgEditAvatar(window.get());
    dialog->show();

    browseForAvatar(dialog, "avatar.png", png());

    EXPECT_FALSE(dialog->getImage().isEmpty());
    EXPECT_EQ(labelSaying(dialog, "No image chosen."), nullptr);
}

TEST_F(LobbyDialogs, AFileThatIsNoImageSaysSo)
{
    auto *dialog = new DlgEditAvatar(window.get());
    dialog->show();

    browseForAvatar(dialog, "not-an-image.png", "not an image");

    EXPECT_NE(labelSaying(dialog, "Invalid image chosen."), nullptr);
    EXPECT_TRUE(dialog->getImage().isEmpty());
}

TEST_F(LobbyDialogs, CancellingThePickerKeepsTheChosenImage)
{
    auto *dialog = new DlgEditAvatar(window.get());
    dialog->show();
    browseForAvatar(dialog, "avatar.png", png());

    browseForAvatar(dialog, QString(), QByteArray());

    EXPECT_FALSE(dialog->getImage().isEmpty());
}

TEST_F(LobbyDialogs, AnImageArrivingAfterTheDialogClosedIsDropped)
{
    auto *dialog = new DlgEditAvatar(window.get());
    dialog->show();
    for (QPushButton *button : dialog->findChildren<QPushButton *>()) {
        if (button->text() == "Browse...") {
            button->click();
        }
    }

    delete dialog;
    picker.pick("avatar.png", png());
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

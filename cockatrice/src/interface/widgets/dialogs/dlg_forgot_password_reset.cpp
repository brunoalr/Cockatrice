#include "dlg_forgot_password_reset.h"

#include "../../../client/settings/cache_settings.h"
#include "../utility/non_blocking_dialogs.h"
#include "forgot_password_server.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QIntValidator>
#include <QLabel>
#include <QSharedPointer>
#include <QSize>
#include <QVBoxLayout>
#include <libcockatrice/settings/servers_settings.h>
#include <libcockatrice/utility/string_limits.h>

class QVBoxLayout;
class QWidget;

DlgForgotPasswordReset::DlgForgotPasswordReset(QWidget *parent)
    : QDialog(parent), tokenEdit(new QLineEdit()), newpasswordEdit(new QLineEdit()),
      newpasswordverifyEdit(new QLineEdit())
{
    const auto [lastfphost, lastfpport, lastfpplayername] = lastForgotPasswordServer();
    rejectWithoutPasswordRequest(this, tr("Reset Password Warning"),
                                 tr("A problem has occurred. Please try to request a new password again."));

    infoLabel = new QLabel(tr("Enter the received token and the new password in order to set your new password."));
    infoLabel->setWordWrap(true);

    hostLabel = new QLabel(tr("&Host:"));
    hostEdit = new QLineEdit(lastfphost);
    hostEdit->setMaxLength(MAX_NAME_LENGTH);
    hostLabel->setBuddy(hostEdit);

    portLabel = new QLabel(tr("&Port:"));
    portEdit = new QLineEdit(lastfpport);
    portEdit->setValidator(new QIntValidator(0, 0xffff, portEdit));
    portLabel->setBuddy(portEdit);

    playernameLabel = new QLabel(tr("Player &name:"));
    playernameEdit = new QLineEdit(lastfpplayername);
    playernameEdit->setMaxLength(MAX_NAME_LENGTH);
    playernameLabel->setBuddy(playernameEdit);

    //: Code mailed to the player so they can reset their password. Not a game token.
    tokenLabel = new QLabel(tr("Token:"));
    tokenEdit->setMaxLength(MAX_NAME_LENGTH);
    tokenLabel->setBuddy(tokenLabel);

    newpasswordLabel = new QLabel(tr("New Password:"));
    newpasswordEdit->setMaxLength(MAX_NAME_LENGTH);
    newpasswordLabel->setBuddy(newpasswordEdit);
    newpasswordEdit->setEchoMode(QLineEdit::Password);

    newpasswordverifyLabel = new QLabel(tr("New Password:"));
    newpasswordverifyEdit->setMaxLength(MAX_NAME_LENGTH);
    newpasswordverifyLabel->setBuddy(newpasswordEdit);
    newpasswordverifyEdit->setEchoMode(QLineEdit::Password);

    if (hasSavedPasswordRequest()) {
        hostLabel->hide();
        hostEdit->hide();
        portLabel->hide();
        portEdit->hide();
        playernameLabel->hide();
        playernameEdit->hide();
    }

    QGridLayout *grid = new QGridLayout;
    grid->addWidget(infoLabel, 0, 0, 1, 2);
    grid->addWidget(hostLabel, 1, 0);
    grid->addWidget(hostEdit, 1, 1);
    grid->addWidget(portLabel, 2, 0);
    grid->addWidget(portEdit, 2, 1);
    grid->addWidget(playernameLabel, 3, 0);
    grid->addWidget(playernameEdit, 3, 1);
    grid->addWidget(tokenLabel, 4, 0);
    grid->addWidget(tokenEdit, 4, 1);
    grid->addWidget(newpasswordLabel, 5, 0);
    grid->addWidget(newpasswordEdit, 5, 1);
    grid->addWidget(newpasswordverifyLabel, 6, 0);
    grid->addWidget(newpasswordverifyEdit, 6, 1);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &DlgForgotPasswordReset::actOk);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &DlgForgotPasswordReset::reject);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(grid);
    mainLayout->addWidget(buttonBox);
    setLayout(mainLayout);

    setWindowTitle(tr("Reset Password"));
    setFixedHeight(sizeHint().height());
    setMinimumWidth(300);
}

void DlgForgotPasswordReset::actOk()
{
    if (playernameEdit->text().isEmpty()) {
        Dialogs::critical(this, tr("Reset Password Error"), tr("The player name can't be empty."));
        return;
    }

    if (tokenEdit->text().isEmpty()) {
        Dialogs::critical(this, tr("Reset Password Error"), tr("The token can't be empty."));
        return;
    }

    if (newpasswordEdit->text().isEmpty()) {
        Dialogs::critical(this, tr("Reset Password Error"), tr("The new password can't be empty."));
        return;
    }

    //! \todo This stuff should be using QValidators.
    if (newpasswordEdit->text().length() < 8) {
        Dialogs::critical(this, tr("Error"), tr("Your password is too short."));
        return;
    }
    if (newpasswordEdit->text() != newpasswordverifyEdit->text()) {
        Dialogs::critical(this, tr("Reset Password Error"), tr("The passwords do not match."));
        return;
    }

    ServersSettings &servers = SettingsCache::instance().servers();
    servers.setFPHostName(hostEdit->text());
    servers.setFPPort(portEdit->text());
    servers.setFPPlayerName(playernameEdit->text());

    accept();
}

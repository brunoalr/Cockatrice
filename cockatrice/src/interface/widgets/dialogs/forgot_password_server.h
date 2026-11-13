#ifndef FORGOT_PASSWORD_SERVER_H
#define FORGOT_PASSWORD_SERVER_H

#include "../../../client/settings/cache_settings.h"
#include "../utility/non_blocking_dialogs.h"

#include <QDialog>
#include <QString>
#include <QTimer>
#include <libcockatrice/settings/servers_settings.h>

struct ForgotPasswordServer
{
    QString host;
    QString port;
    QString playerName;
};

inline ForgotPasswordServer lastForgotPasswordServer()
{
    const ServersSettings &servers = SettingsCache::instance().servers();
    if (!servers.getFPHostname().isEmpty() && !servers.getFPPort().isEmpty() && !servers.getFPPlayerName().isEmpty()) {
        return {servers.getFPHostname(), servers.getFPPort(), servers.getFPPlayerName()};
    }
    return {servers.getHostname(), servers.getPort(), servers.getPlayerName()};
}

inline void rejectWithoutPasswordRequest(QDialog *dialog, const QString &title, const QString &text)
{
    const ServersSettings &servers = SettingsCache::instance().servers();
    if (!servers.getFPHostname().isEmpty() || !servers.getFPPort().isEmpty() || !servers.getFPPlayerName().isEmpty()) {
        return;
    }
    QTimer::singleShot(0, dialog, [dialog, title, text] { Dialogs::warning(dialog, title, text); });
    dialog->reject();
}

#endif

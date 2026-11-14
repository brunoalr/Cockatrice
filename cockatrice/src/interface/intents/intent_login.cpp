#include "intent_login.h"

#include "../../client/settings/cache_settings.h"
#include "../widgets/dialogs/dlg_login_prompt.h"
#include "../widgets/utility/non_blocking_dialogs.h"
#include "contexts/context_connect_to_server.h"
#include "libcockatrice/settings/servers_settings.h"

#include <QDialog>
#include <QString>
#include <QVariant>
#include <memory>

static void saveLogin(const ContextConnectToServer &context)
{
    ServersSettings &servers = SettingsCache::instance().servers();
    // The host may already be saved under a friendly name (e.g. a public-server
    // list entry) with no credentials; reuse that name instead of overwriting
    // it with the raw hostname when addNewServer updates the entry in place.
    QString saveName = context.hostname;
    const int existingIndex = servers.findServerIndex(context.hostname, context.port);
    if (const auto existing = servers.getServer(existingIndex)) {
        saveName = existing->saveName;
        if (saveName.isEmpty()) {
            saveName = context.hostname;
        }
    }
    servers.addNewServer(saveName, context.hostname, context.port, context.username, context.password, true);
}

IntentGetLoginCredentials::IntentGetLoginCredentials(ContextConnectToServer *_context,
                                                     bool _promptForMissingCredentials)
    : Intent(), context(_context), promptForMissingCredentials(_promptForMissingCredentials)
{
}

bool IntentGetLoginCredentials::checkPrecondition() const
{
    ServersSettings &servers = SettingsCache::instance().servers();
    return servers.hasLoginData(context->hostname, context->port);
}

void IntentGetLoginCredentials::onPreconditionSatisfied()
{
    ServersSettings &servers = SettingsCache::instance().servers();
    const int index = servers.findServerIndex(context->hostname, context->port);

    if (const auto server = servers.getServer(index)) {
        context->username = server->username;
        context->password = server->password;
        emitFinished();
    } else {
        emitFailed(tr("No saved credentials for this server"));
    }
}

void IntentGetLoginCredentials::onPreconditionNotSatisfied()
{
    // MainWindow::applyStartupDestination runs this intent on every launch for
    // users whose startup tab is Server / Server Room; keep that path quiet, as
    // it was before the link-driven sign-in dialog existed.
    if (!promptForMissingCredentials) {
        emitFailed(tr("No saved credentials for this server"));
        return;
    }

    // No credentials saved for the target server: ask the user for them. They
    // opt into saving them so later links to the same server connect directly.
    const QString serverText = context->hostname + ":" + context->port;
    auto prompt = std::make_unique<DlgLoginPrompt>(serverText);
    const DlgLoginPrompt *dialog = prompt.get();

    Dialogs::open(prompt.release(), Dialogs::whileAlive(this, [this, dialog](int result) {
                      if (result != QDialog::Accepted) {
                          emitCancelled();
                          return;
                      }

                      context->username = dialog->username();
                      context->password = dialog->password();
                      if (dialog->savePassword() && !context->username.isEmpty()) {
                          saveLogin(*context);
                      }

                      emitFinished();
                  }));
}

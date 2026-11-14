#ifndef SETTINGS_CLOSE_CHECK_H
#define SETTINGS_CLOSE_CHECK_H

#include "../../shutdown_chain.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <functional>

class QDialog;

inline Q_LOGGING_CATEGORY(DlgSettingsLog, "dlg_settings");

// Yes to a question ("go back") keeps the dialog open; No to every one closes it.
class SettingsCloseCheck : public QObject
{
    Q_DECLARE_TR_FUNCTIONS(DlgSettings)

public:
    explicit SettingsCloseCheck(QDialog *dialog);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QDialog *dialog;
    ShutdownChain chain;

    bool mayClose();
    ShutdownChain::Step askToGoBackIf(std::function<bool()> unusable, const QString &question);
};

#endif

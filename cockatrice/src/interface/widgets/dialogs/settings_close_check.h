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

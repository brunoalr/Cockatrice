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

#ifndef COCKATRICE_TEST_TEST_HOME_H
#define COCKATRICE_TEST_TEST_HOME_H

#include <QDebug>
#include <QLatin1Char>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

namespace test_support
{
// Create it before anything resolves a path: QSettings keeps the user folder it finds first.
class TestHome
{
public:
    TestHome()
    {
        qputenv("HOME", dir.path().toUtf8());
#ifdef Q_OS_MACOS
        // The standard paths ignore HOME on macOS.
        qputenv("CFFIXED_USER_HOME", dir.path().toUtf8());
        // The native format on macOS is CFPreferences, which writes to the real Library whatever the home is.
        QSettings::setDefaultFormat(QSettings::IniFormat);
#endif
        QStandardPaths::setTestModeEnabled(true);
    }

    [[nodiscard]] bool writesStayInside() const
    {
#ifndef Q_OS_WIN
        // Not on Windows: its known folders follow no environment variable, so they cannot be moved for a test.
        for (const QString &path :
             {QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
              QStandardPaths::writableLocation(QStandardPaths::CacheLocation),
              QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), QSettings().fileName()}) {
            if (!dir.isValid() || !path.startsWith(dir.path() + QLatin1Char('/'))) {
                qCritical() << "the test would write to" << path << "outside the temporary home: refusing to run";
                return false;
            }
        }
#endif
        return true;
    }

private:
    const QTemporaryDir dir;
};
} // namespace test_support

#endif

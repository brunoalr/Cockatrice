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

#ifndef SHUTDOWN_CHAIN_H
#define SHUTDOWN_CHAIN_H

#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <functional>

class ShutdownChain : public QObject
{
public:
    enum class State
    {
        Idle,
        Confirming,
        Accepted,
    };

    using Answer = std::function<void(bool yes)>;

    struct Step
    {
        QPointer<QObject> owner; // destroyed before it answers counts as a yes
        std::function<void(const Answer &answer)> ask;
    };

    explicit ShutdownChain(QObject *parent = nullptr);

    [[nodiscard]] State state() const
    {
        return currentState;
    }

    // Reports as soon as it is decided: inside start() when every step answers there.
    bool start(QList<Step> steps, std::function<void(bool accepted)> finished);

private:
    void askNext();
    void answer(quint64 question, bool yes);
    void finish(bool accepted);

    State currentState = State::Idle;
    QList<Step> pendingSteps;
    std::function<void(bool accepted)> onFinished;
    quint64 currentQuestion = 0;
    QMetaObject::Connection ownerWatch;
};

#endif // SHUTDOWN_CHAIN_H

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

#include "shutdown_chain.h"

#include <utility>

ShutdownChain::ShutdownChain(QObject *parent) : QObject(parent)
{
}

bool ShutdownChain::start(QList<Step> steps, std::function<void(bool accepted)> finished)
{
    if (currentState != State::Idle) {
        return false;
    }
    currentState = State::Confirming;
    pendingSteps = std::move(steps);
    onFinished = std::move(finished);
    askNext();
    return true;
}

void ShutdownChain::askNext()
{
    while (!pendingSteps.isEmpty() && !pendingSteps.first().owner) {
        pendingSteps.removeFirst();
    }
    if (pendingSteps.isEmpty()) {
        finish(true);
        return;
    }
    const Step step = pendingSteps.takeFirst();
    const quint64 question = ++currentQuestion;
    // Queued: the owner is still being destroyed, and the next step may open a dialog.
    ownerWatch = connect(
        step.owner, &QObject::destroyed, this, [this, question] { answer(question, true); }, Qt::QueuedConnection);
    step.ask([guard = QPointer<ShutdownChain>(this), question](bool yes) {
        if (guard) {
            guard->answer(question, yes);
        }
    });
}

void ShutdownChain::answer(quint64 question, bool yes)
{
    if (currentState != State::Confirming || question != currentQuestion) {
        return;
    }
    disconnect(ownerWatch);
    if (yes) {
        askNext();
    } else {
        finish(false);
    }
}

void ShutdownChain::finish(bool accepted)
{
    ++currentQuestion;
    pendingSteps.clear();
    currentState = accepted ? State::Accepted : State::Idle;
    if (const auto finished = std::exchange(onFinished, {})) {
        finished(accepted);
    }
}

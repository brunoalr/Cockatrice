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

    // Reports from the event loop, never inside start(): Qt ignores a close() made inside the window's closeEvent().
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

#include "commander_spellbook_api_accessor.h"

#include "../../../../../client/network/web_relay.h"
#include "api_response/commander_spellbook_deck_request.h"
#include "api_response/commander_spellbook_estimate_bracket_result.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStringLiteral>
#include <QUrl>
#include <libcockatrice/network/network_manager.h>
#include <version_string.h>

class DeckList;

static const QUrl ESTIMATE_BRACKET_URL(QStringLiteral("https://backend.commanderspellbook.com/estimate-bracket"));

CommanderSpellbookApiAccessor &CommanderSpellbookApiAccessor::instance()
{
    static CommanderSpellbookApiAccessor instance;
    return instance;
}

bool CommanderSpellbookApiAccessor::isReachable()
{
    return WebRelay::reaches(ESTIMATE_BRACKET_URL);
}

CommanderSpellbookApiAccessor::CommanderSpellbookApiAccessor(QObject *parent)
    : QObject(parent), network(NetworkManagerFactory::create(this))
{
}

CommanderSpellbookApiAccessor::RequestId CommanderSpellbookApiAccessor::estimateBracket(const DeckList &deck,
                                                                                        QObject *requester)
{
    if (!requester) {
        return 0;
    }

    CommanderSpellbookDeckRequest deckRequest = CommanderSpellbookDeckRequest::fromDeckList(deck);

    QJsonDocument doc(deckRequest.toJson());
    QByteArray body = doc.toJson(QJsonDocument::Compact);

    QNetworkRequest req(WebRelay::route(ESTIMATE_BRACKET_URL));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
#ifndef Q_OS_WASM
    // A browser preflights a named User-Agent, and the relay answers a preflight for Content-Type only.
    req.setHeader(QNetworkRequest::UserAgentHeader, QString("Cockatrice %1").arg(VERSION_STRING));
#endif

    QNetworkReply *reply = network->post(req, body);

    const RequestId id = nextRequestId++;

    pendingRequests.insert(id, reply);
    const QPointer<QObject> guardedRequester(requester);
    connect(requester, &QObject::destroyed, reply, [this, id]() { cancelEstimate(id); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, guardedRequester]() {
        if (pendingRequests.take(id) != reply) {
            return;
        }
        reply->deleteLater();
        if (guardedRequester) {
            onEstimateReplyFinished(reply, id, guardedRequester);
        }
    });

    return id;
}

void CommanderSpellbookApiAccessor::cancelEstimate(RequestId id)
{
    const QPointer<QNetworkReply> reply = pendingRequests.take(id);
    if (!reply) {
        return;
    }
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void CommanderSpellbookApiAccessor::onEstimateReplyFinished(QNetworkReply *reply, RequestId id, QObject *requester)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit estimateBracketError(id, requester, reply->errorString());
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);

    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit estimateBracketError(id, requester, QStringLiteral("Invalid JSON response"));
        return;
    }

    EstimateBracketResult result = EstimateBracketResult::fromJson(doc.object());
    if (result.bracketTag.trimmed().isEmpty()) {
        emit estimateBracketError(id, requester, QStringLiteral("Missing bracket in response"));
        return;
    }

    emit estimateBracketFinished(id, requester, result);
}

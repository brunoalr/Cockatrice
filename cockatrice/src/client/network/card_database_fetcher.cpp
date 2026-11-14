#include "card_database_fetcher.h"

#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <libcockatrice/network/network_manager.h>
#include <utility>

namespace
{
const QString cardsFile = QStringLiteral("cards.xml");
const QString tokensFile = QStringLiteral("tokens.xml");
const int wholeDownloadLimitMs = 5 * 60 * 1000;
} // namespace

CardDatabaseFetcher::CardDatabaseFetcher(const QUrl &_base, const QString &_targetDir, QObject *parent)
    : QObject(parent), network(NetworkManagerFactory::create(this)), base(_base), targetDir(_targetDir)
{
}

void CardDatabaseFetcher::fetch()
{
    if (!running.isEmpty()) {
        return;
    }
    elapsed.start();
    for (const QString &name : {cardsFile, tokensFile}) {
        QNetworkRequest request(base.resolved(QUrl(name)));
        // A browser applies this limit to the whole download, not to a stall.
        request.setTransferTimeout(wholeDownloadLimitMs);
        qCInfo(CardDatabaseFetcherLog) << "Downloading" << request.url().toDisplayString();
        QNetworkReply *reply = network->get(request);
        running.append(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, name] { replyFinished(reply, name); });
    }
}

void CardDatabaseFetcher::replyFinished(QNetworkReply *reply, const QString &name)
{
    reply->deleteLater();
    if (!running.removeOne(reply)) {
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        fail(QStringLiteral("%1: %2").arg(name, reply->errorString()));
        return;
    }
    const QByteArray body = reply->readAll();
    // Some hosts answer a missing file with status 200 and a page of their own; a cut transfer lacks the end.
    if (!body.left(1024).contains("<cockatrice_carddatabase") ||
        !body.right(256).contains("</cockatrice_carddatabase>")) {
        fail(tr("%1: not a card database").arg(name));
        return;
    }
    downloaded.insert(name, body);
    if (running.isEmpty()) {
        save();
    }
}

void CardDatabaseFetcher::save()
{
    if (!QDir().mkpath(targetDir)) {
        fail(tr("cannot create %1").arg(targetDir));
        return;
    }
    // tokens.xml first: a failure in between must not leave a cards.xml without it.
    for (const QString &name : {tokensFile, cardsFile}) {
        QSaveFile file(QDir(targetDir).filePath(name));
        const QByteArray data = downloaded.value(name);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            fail(QStringLiteral("%1: %2").arg(file.fileName(), file.errorString()));
            return;
        }
    }
    qCInfo(CardDatabaseFetcherLog) << "Card database downloaded:" << downloaded.value(cardsFile).size() << "and"
                                   << downloaded.value(tokensFile).size() << "bytes in" << elapsed.elapsed() << "ms";
    downloaded.clear();
    emit fetched();
}

void CardDatabaseFetcher::fail(const QString &what)
{
    const QList<QNetworkReply *> aborted = std::exchange(running, {});
    for (QNetworkReply *reply : aborted) {
        reply->abort();
        reply->deleteLater();
    }
    downloaded.clear();
    qCWarning(CardDatabaseFetcherLog) << "Card database download failed:" << what;
    emit failed(what);
}

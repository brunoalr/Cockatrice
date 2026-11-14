#ifndef CARD_DATABASE_FETCHER_H
#define CARD_DATABASE_FETCHER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QUrl>

inline Q_LOGGING_CATEGORY(CardDatabaseFetcherLog, "card_database.fetcher");

class QNetworkAccessManager;
class QNetworkReply;

class CardDatabaseFetcher : public QObject
{
    Q_OBJECT

public:
    CardDatabaseFetcher(const QUrl &base, const QString &targetDir, QObject *parent = nullptr);

    void fetch();

signals:
    void fetched();
    void failed(const QString &what);

private:
    void replyFinished(QNetworkReply *reply, const QString &name);
    void save();
    void fail(const QString &what);

    QNetworkAccessManager *network;
    QUrl base;
    QString targetDir;
    QList<QNetworkReply *> running;
    QHash<QString, QByteArray> downloaded;
    QElapsedTimer elapsed;
};

#endif // CARD_DATABASE_FETCHER_H

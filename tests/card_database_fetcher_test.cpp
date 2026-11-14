#include "client/network/card_database_fetcher.h"
#include "dialog_test_support.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <gtest/gtest.h>

namespace
{
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(data), data.size());
}

class CardDatabaseFetcherTest : public ::testing::Test // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QTemporaryDir site;
    QTemporaryDir target;
    // As in the published files, the root element starts some hundred bytes into the document.
    const QByteArray prolog =
        QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!-- ") + QByteArray(300, 'x') + QByteArray(" -->\n");
    const QByteArray cards = prolog + QByteArray("<cockatrice_carddatabase version=\"4\"><cards>") +
                             QByteArray("<card><name>Example</name></card>").repeated(5000) +
                             QByteArray("</cards></cockatrice_carddatabase>\n");
    const QByteArray tokens = prolog + "<cockatrice_carddatabase version=\"4\"><cards/></cockatrice_carddatabase>\n";

    // The web build passes the page's own address: the files sit beside it.
    QUrl page() const
    {
        return QUrl::fromLocalFile(site.filePath(QStringLiteral("index.html")));
    }

    void publishCards() const
    {
        writeFile(site.filePath(QStringLiteral("cards.xml")), cards);
    }

    void publishTokens() const
    {
        writeFile(site.filePath(QStringLiteral("tokens.xml")), tokens);
    }

    bool targetIsEmpty() const
    {
        return QDir(target.path()).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    }

    void expectAFailureThatNames(const QString &name) const
    {
        CardDatabaseFetcher fetcher(page(), target.path());
        const QSignalSpy fetched(&fetcher, &CardDatabaseFetcher::fetched);
        QSignalSpy failed(&fetcher, &CardDatabaseFetcher::failed);

        fetcher.fetch();

        ASSERT_TRUE(failed.wait(5000));
        EXPECT_TRUE(failed.first().first().toString().contains(name));
        QTest::qWait(200);
        EXPECT_TRUE(fetched.isEmpty());
        EXPECT_TRUE(targetIsEmpty());
    }
};

TEST_F(CardDatabaseFetcherTest, BothFilesGiveFetchedAndTwoIdenticalCopies)
{
    publishCards();
    publishTokens();
    CardDatabaseFetcher fetcher(page(), target.path());
    QSignalSpy fetched(&fetcher, &CardDatabaseFetcher::fetched);
    QSignalSpy failed(&fetcher, &CardDatabaseFetcher::failed);

    fetcher.fetch();

    ASSERT_TRUE(fetched.wait(5000));
    EXPECT_TRUE(failed.isEmpty());
    EXPECT_EQ(readFile(target.filePath(QStringLiteral("cards.xml"))), cards);
    EXPECT_EQ(readFile(target.filePath(QStringLiteral("tokens.xml"))), tokens);
}

TEST_F(CardDatabaseFetcherTest, MissingTokensGiveFailedAndLeaveNoCardsBehind)
{
    publishCards();

    expectAFailureThatNames(QStringLiteral("tokens.xml"));
}

TEST_F(CardDatabaseFetcherTest, MissingCardsGiveFailedAndLeaveNoTokensBehind)
{
    publishTokens();

    expectAFailureThatNames(QStringLiteral("cards.xml"));
}

TEST_F(CardDatabaseFetcherTest, ABodyThatIsNotACardDatabaseGivesFailedAndLeavesNothingBehind)
{
    publishCards();
    writeFile(site.filePath(QStringLiteral("tokens.xml")), "<!doctype html><html><body>Not found</body></html>\n");

    expectAFailureThatNames(QStringLiteral("tokens.xml"));
}

TEST_F(CardDatabaseFetcherTest, ABodyWithoutTheRootElementAtItsStartGivesFailed)
{
    publishCards();
    writeFile(site.filePath(QStringLiteral("tokens.xml")),
              QByteArray("<html>") + QByteArray(2000, ' ') + QByteArray("</cockatrice_carddatabase>\n"));

    expectAFailureThatNames(QStringLiteral("tokens.xml"));
}

TEST_F(CardDatabaseFetcherTest, ADatabaseCutShortGivesFailedAndLeavesNothingBehind)
{
    writeFile(site.filePath(QStringLiteral("cards.xml")), cards.left(cards.size() / 2));
    publishTokens();

    expectAFailureThatNames(QStringLiteral("cards.xml"));
}

TEST_F(CardDatabaseFetcherTest, ATokensFileThatCannotBeSavedLeavesNoCardsBehind)
{
    publishCards();
    publishTokens();
    ASSERT_TRUE(QDir(target.path()).mkdir(QStringLiteral("tokens.xml")));
    CardDatabaseFetcher fetcher(page(), target.path());
    QSignalSpy fetched(&fetcher, &CardDatabaseFetcher::fetched);
    QSignalSpy failed(&fetcher, &CardDatabaseFetcher::failed);

    fetcher.fetch();

    ASSERT_TRUE(failed.wait(5000));
    EXPECT_FALSE(fetched.wait(200));
    EXPECT_FALSE(QFile::exists(target.filePath(QStringLiteral("cards.xml"))));
}

TEST_F(CardDatabaseFetcherTest, AQueryAndAFragmentInThePageAddressAreLeftOut)
{
    publishCards();
    publishTokens();
    QUrl address = QUrl::fromLocalFile(site.path() + QLatin1Char('/'));
    address.setQuery(QStringLiteral("connect=example"));
    address.setFragment(QStringLiteral("part"));
    CardDatabaseFetcher fetcher(address, target.path());
    const auto *network = fetcher.findChild<QNetworkAccessManager *>();
    ASSERT_NE(network, nullptr);
    QList<QUrl> requested;
    QObject::connect(network, &QNetworkAccessManager::finished,
                     [&requested](const QNetworkReply *reply) { requested.append(reply->request().url()); });
    QSignalSpy fetched(&fetcher, &CardDatabaseFetcher::fetched);

    fetcher.fetch();

    ASSERT_TRUE(fetched.wait(5000));
    ASSERT_EQ(requested.size(), 2);
    for (const QUrl &url : requested) {
        EXPECT_FALSE(url.hasQuery()) << url.toString().toStdString();
        EXPECT_FALSE(url.hasFragment()) << url.toString().toStdString();
    }
}

TEST_F(CardDatabaseFetcherTest, SecondFetchWhileOneRunsStartsNothing)
{
    publishCards();
    publishTokens();
    CardDatabaseFetcher fetcher(page(), target.path());
    auto *network = fetcher.findChild<QNetworkAccessManager *>();
    ASSERT_NE(network, nullptr);
    QList<QUrl> requested;
    QObject::connect(network, &QNetworkAccessManager::finished,
                     [&requested](const QNetworkReply *reply) { requested.append(reply->url()); });
    QSignalSpy fetched(&fetcher, &CardDatabaseFetcher::fetched);

    fetcher.fetch();
    fetcher.fetch();

    ASSERT_TRUE(fetched.wait(5000));
    EXPECT_FALSE(fetched.wait(200));
    EXPECT_EQ(fetched.size(), 1);
    EXPECT_EQ(requested.size(), 2);
}

TEST_F(CardDatabaseFetcherTest, FetchAfterAFailureTriesAgain)
{
    publishCards();
    CardDatabaseFetcher fetcher(page(), target.path());
    QSignalSpy fetched(&fetcher, &CardDatabaseFetcher::fetched);
    QSignalSpy failed(&fetcher, &CardDatabaseFetcher::failed);
    fetcher.fetch();
    ASSERT_TRUE(failed.wait(5000));

    publishTokens();
    fetcher.fetch();

    ASSERT_TRUE(fetched.wait(5000));
    EXPECT_EQ(failed.size(), 1);
    EXPECT_EQ(readFile(target.filePath(QStringLiteral("cards.xml"))), cards);
    EXPECT_EQ(readFile(target.filePath(QStringLiteral("tokens.xml"))), tokens);
}

// A User-Agent or Accept header can make a browser send a cross-origin preflight first.
TEST_F(CardDatabaseFetcherTest, RequestsCarryNoUserAgentOrAcceptHeader)
{
    publishCards();
    publishTokens();
    CardDatabaseFetcher fetcher(page(), target.path());
    auto *network = fetcher.findChild<QNetworkAccessManager *>();
    ASSERT_NE(network, nullptr);
    QList<QNetworkRequest> requests;
    QObject::connect(network, &QNetworkAccessManager::finished,
                     [&requests](const QNetworkReply *reply) { requests.append(reply->request()); });
    QSignalSpy fetched(&fetcher, &CardDatabaseFetcher::fetched);

    fetcher.fetch();

    ASSERT_TRUE(fetched.wait(5000));
    ASSERT_EQ(requests.size(), 2);
    for (const QNetworkRequest &request : requests) {
        EXPECT_FALSE(request.header(QNetworkRequest::UserAgentHeader).isValid())
            << request.url().toString().toStdString();
        EXPECT_FALSE(request.hasRawHeader("User-Agent")) << request.url().toString().toStdString();
        EXPECT_FALSE(request.hasRawHeader("Accept")) << request.url().toString().toStdString();
    }
}
} // namespace

int main(int argc, char **argv)
{
    return DialogTest::run(argc, argv, QStringLiteral("card-database-fetcher-test"));
}

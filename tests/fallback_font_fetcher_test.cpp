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

#include "client/network/fallback_font_fetcher.h"
#include "dialog_test_support.h"

#include <QByteArray>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QWidget>
#include <gtest/gtest.h>
#include <initializer_list>

namespace
{
QByteArray bigEndian16(std::initializer_list<int> values)
{
    QByteArray out;
    for (int value : values) {
        out.append(char(value >> 8)).append(char(value));
    }
    return out;
}

quint32 checksum(const QByteArray &table)
{
    const QByteArray padded = table + QByteArray((4 - table.size() % 4) % 4, '\0');
    quint32 sum = 0;
    for (qsizetype i = 0; i < padded.size(); i += 4) {
        sum += quint32(quint8(padded[i])) << 24 | quint32(quint8(padded[i + 1])) << 16 |
               quint32(quint8(padded[i + 2])) << 8 | quint8(padded[i + 3]);
    }
    return sum;
}

QByteArray bigEndian32(quint32 value)
{
    return bigEndian16({int(value >> 16), int(value & 0xffff)});
}

// The smallest TrueType font the platforms accept: two square glyphs, one of them for 'A'.
QByteArray tinyFont(const QString &family, qsizetype padding = 0)
{
    const QByteArray square = bigEndian16({1, 100, 0, 500, 400, 3, 0}) + QByteArray(4, '\x01') +
                              bigEndian16({100, 0, 400, 0, 0, 400, 0, -400});
    QByteArray names = bigEndian16({0, 4, 6 + 4 * 12});
    QByteArray strings;
    for (const auto &[id, text] : {std::pair{1, family}, {2, QStringLiteral("Regular")}, {4, family}, {6, family}}) {
        QByteArray utf16;
        for (const QChar c : text) {
            utf16 += bigEndian16({c.unicode()});
        }
        names += bigEndian16({3, 1, 0x409, id, int(utf16.size()), int(strings.size())});
        strings += utf16;
    }
    QMap<QByteArray, QByteArray> tables = {
        {"OS/2", bigEndian16({4, 600, 400, 5, 0, 650, 600, 0, 75, 650, 600, 0, 350, 50, 300, 0}) +
                     QByteArray(10, '\0') + bigEndian16({0, 1, 0, 0, 0, 0, 0, 0}) + "NONE" +
                     bigEndian16({0x40, 0x41, 0x41, 800, -200, 0, 800, 200, 0, 1, 0, 0, 400, 400, 0, 0x20, 0})},
        {"cmap",
         bigEndian16({0, 1, 3, 1, 0, 12, 4, 32, 0, 4, 4, 1, 0, 0x41, 0xffff, 0, 0x41, 0xffff, 1 - 0x41, 1, 0, 0})},
        {"glyf", square + square},
        {"head", bigEndian16({1, 0, 1, 0, 0, 0, 0x5f0f, 0x3cf5, 3, 1000}) + QByteArray(16, '\0') +
                     bigEndian16({100, 0, 500, 400, 0, 8, 2, 0, 0})},
        {"hhea", bigEndian16({1, 0, 800, -200, 0, 600, 100, 100, 500, 1, 0, 0, 0, 0, 0, 0, 0, 2})},
        {"hmtx", bigEndian16({600, 100, 600, 100})},
        {"loca", bigEndian16({0, 17, 34})},
        {"maxp", bigEndian16({1, 0, 2, 4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0})},
        {"name", names + strings},
        {"post", bigEndian16({3, 0, 0, 0, -100, 50}) + QByteArray(20, '\0')},
    };
    if (padding > 0) {
        tables.insert("zpad", QByteArray(padding, '\0'));
    }
    QByteArray font = bigEndian16({1, 0, int(tables.size()), 128, 3, int(tables.size()) * 16 - 128});
    QByteArray body;
    qsizetype head = 0;
    for (auto table = tables.cbegin(); table != tables.cend(); ++table) {
        const qsizetype offset = 12 + 16 * tables.size() + body.size();
        if (table.key() == "head") {
            head = offset;
        }
        font += table.key() + bigEndian32(checksum(table.value())) + bigEndian32(offset) +
                bigEndian32(table.value().size());
        body += table.value() + QByteArray((4 - table.value().size() % 4) % 4, '\0');
    }
    font += body;
    font.replace(head + 8, 4, bigEndian32(0xb1b0afba - checksum(font)));
    return font;
}

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(data), data.size());
}

QStringList logged;

void keepFetcherMessages(QtMsgType, const QMessageLogContext &context, const QString &message)
{
    if (QLatin1String(context.category) == QLatin1String("fallback_font_fetcher")) {
        logged.append(message);
    }
}

class FallbackFontFetcherTest : public ::testing::Test // NOSONAR: TEST_F reads fixture members from a subclass
{
protected:
    QTemporaryDir site;

    void SetUp() override
    {
        ASSERT_TRUE(QDir(site.path()).mkdir(QStringLiteral("fonts")));
    }

    void publish(const QString &name, const QByteArray &data) const
    {
        writeFile(site.filePath(QStringLiteral("fonts/") + name), data);
    }

    void publishManifest(const QStringList &lines) const
    {
        publish(QStringLiteral("index.txt"), lines.join(QLatin1Char('\n')).toUtf8() + '\n');
    }

    QStringList fetchAll(int calls = 1) const
    {
        FallbackFontFetcher fetcher(QUrl::fromLocalFile(site.filePath(QStringLiteral("index.html"))));
        const auto *network = fetcher.findChild<QNetworkAccessManager *>();
        EXPECT_NE(network, nullptr);
        QStringList requested;
        if (network) {
            QObject::connect(network, &QNetworkAccessManager::finished, [this, &requested](const QNetworkReply *reply) {
                requested.append(QDir(site.path()).relativeFilePath(reply->url().toLocalFile()));
            });
        }
        QSignalSpy finished(&fetcher, &FallbackFontFetcher::finished);
        logged.clear();
        const QtMessageHandler previous = qInstallMessageHandler(keepFetcherMessages);
        for (int call = 0; call < calls; ++call) {
            fetcher.fetch();
        }
        EXPECT_TRUE(finished.wait(10000));
        QTest::qWait(50);
        EXPECT_EQ(finished.count(), 1);
        qInstallMessageHandler(previous);
        return requested;
    }
};

TEST_F(FallbackFontFetcherTest, FontsAreFetchedOneAfterAnotherInTheManifestOrder)
{
    publish(QStringLiteral("b.ttf"), tinyFont(QStringLiteral("Fetcher Order B")));
    publish(QStringLiteral("a.otf"), tinyFont(QStringLiteral("Fetcher Order A")));
    publishManifest({QStringLiteral("b.ttf"), QString(), QStringLiteral("a.otf")});

    EXPECT_EQ(fetchAll().join(QLatin1Char(' ')).toStdString(), "fonts/index.txt fonts/b.ttf fonts/a.otf");
    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher Order B")));
    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher Order A")));
}

// In the browser, text laid out between two added fonts would never take the second one.
TEST_F(FallbackFontFetcherTest, FontsAreAddedTogetherOnceTheLastIsDownloaded)
{
    publish(QStringLiteral("first.ttf"), tinyFont(QStringLiteral("Fetcher Together First")));
    publish(QStringLiteral("last.ttf"), tinyFont(QStringLiteral("Fetcher Together Last")));
    publishManifest({QStringLiteral("first.ttf"), QStringLiteral("last.ttf")});
    FallbackFontFetcher fetcher(QUrl::fromLocalFile(site.filePath(QStringLiteral("index.html"))));
    const auto *network = fetcher.findChild<QNetworkAccessManager *>();
    ASSERT_NE(network, nullptr);
    bool firstAddedEarly = false;
    QObject::connect(network, &QNetworkAccessManager::finished, [&firstAddedEarly](const QNetworkReply *reply) {
        if (reply->url().fileName() == QStringLiteral("last.ttf")) {
            firstAddedEarly = QFontDatabase::families().contains(QStringLiteral("Fetcher Together First"));
        }
    });
    QSignalSpy finished(&fetcher, &FallbackFontFetcher::finished);

    fetcher.fetch();

    ASSERT_TRUE(finished.wait(10000));
    EXPECT_FALSE(firstAddedEarly);
    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher Together First")));
    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher Together Last")));
}

TEST_F(FallbackFontFetcherTest, AMissingManifestFetchesNothingElseAndLogsOneLine)
{
    publish(QStringLiteral("a.ttf"), tinyFont(QStringLiteral("Fetcher Unlisted")));

    EXPECT_EQ(fetchAll().join(QLatin1Char(' ')).toStdString(), "fonts/index.txt");
    EXPECT_EQ(logged.size(), 1) << logged.join(QLatin1Char('\n')).toStdString();
    EXPECT_FALSE(QFontDatabase::families().contains(QStringLiteral("Fetcher Unlisted")));
}

TEST_F(FallbackFontFetcherTest, EntriesThatAreNotPlainFileNamesAreRefused)
{
    publish(QStringLiteral("good.ttf"), tinyFont(QStringLiteral("Fetcher Plain")));
    publishManifest({QStringLiteral("../cards.xml"), QStringLiteral("/fonts/good.ttf"),
                     QStringLiteral("http://127.0.0.1:9/good.ttf"), QStringLiteral("//127.0.0.1:9/good.ttf"),
                     QStringLiteral("sub/good.ttf"), QStringLiteral("%2e%2e%2fcards.xml"),
                     QStringLiteral("..\\cards.xml"), QStringLiteral(".."), QStringLiteral("good.ttf?x"),
                     QStringLiteral("good.ttf#x"), QStringLiteral("good.ttf")});

    EXPECT_EQ(fetchAll().join(QLatin1Char(' ')).toStdString(), "fonts/index.txt fonts/good.ttf");
    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher Plain")));
}

TEST_F(FallbackFontFetcherTest, NothingOutsideTheFontsDirectoryIsFetched)
{
    writeFile(site.filePath(QStringLiteral("outside.ttf")), tinyFont(QStringLiteral("Fetcher Outside")));
    publish(QStringLiteral("inside.ttf"), tinyFont(QStringLiteral("Fetcher Inside")));
    publishManifest({QStringLiteral("../outside.ttf"), QStringLiteral("%2E%2E/outside.ttf"),
                     QUrl::fromLocalFile(site.filePath(QStringLiteral("outside.ttf"))).toString(),
                     site.filePath(QStringLiteral("outside.ttf")), QStringLiteral("inside.ttf")});

    const QStringList requested = fetchAll();

    for (const QString &path : requested) {
        EXPECT_TRUE(path.startsWith(QStringLiteral("fonts/"))) << path.toStdString();
    }
    EXPECT_FALSE(QFontDatabase::families().contains(QStringLiteral("Fetcher Outside")));
    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher Inside")));
}

TEST_F(FallbackFontFetcherTest, ABrokenOrMissingFontIsSkippedAndTheNextOneRegistered)
{
    publish(QStringLiteral("broken.ttf"), "<!doctype html><html><body>Not found</body></html>\n");
    publish(QStringLiteral("good.ttf"), tinyFont(QStringLiteral("Fetcher After Broken")));
    publishManifest({QStringLiteral("broken.ttf"), QStringLiteral("missing.ttf"), QStringLiteral("good.ttf")});

    EXPECT_EQ(fetchAll().join(QLatin1Char(' ')).toStdString(),
              "fonts/index.txt fonts/broken.ttf fonts/missing.ttf fonts/good.ttf");
    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher After Broken")));
}

TEST_F(FallbackFontFetcherTest, EntriesBeyondTheLimitAreNotFetched)
{
    QStringList manifest;
    for (int i = 0; i <= FallbackFontFetcher::maxFonts; ++i) {
        manifest.append(QStringLiteral("font%1.ttf").arg(i));
    }
    publishManifest(manifest);

    EXPECT_EQ(fetchAll().size(), 1 + FallbackFontFetcher::maxFonts);
}

TEST_F(FallbackFontFetcherTest, AFontLargerThanTheLimitIsNotRegistered)
{
    publish(QStringLiteral("large.ttf"),
            tinyFont(QStringLiteral("Fetcher Too Large"), FallbackFontFetcher::maxFontBytes));
    publish(QStringLiteral("small.ttf"), tinyFont(QStringLiteral("Fetcher Small")));
    publishManifest({QStringLiteral("large.ttf"), QStringLiteral("small.ttf")});

    fetchAll();

    EXPECT_FALSE(QFontDatabase::families().contains(QStringLiteral("Fetcher Too Large")));
    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher Small")));
}

TEST_F(FallbackFontFetcherTest, ASecondFetchWhileTheFirstRunsStartsNothing)
{
    publish(QStringLiteral("a.ttf"), tinyFont(QStringLiteral("Fetcher Twice")));
    publishManifest({QStringLiteral("a.ttf")});

    EXPECT_EQ(fetchAll(2).join(QLatin1Char(' ')).toStdString(), "fonts/index.txt fonts/a.ttf");
}

TEST_F(FallbackFontFetcherTest, TheEarlierFontsAreRegisteredWhenTheLastOneFails)
{
    publish(QStringLiteral("good.ttf"), tinyFont(QStringLiteral("Fetcher Before Missing")));
    publishManifest({QStringLiteral("good.ttf"), QStringLiteral("missing.ttf")});

    fetchAll();

    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher Before Missing")));
}

TEST_F(FallbackFontFetcherTest, AManifestLargerThanTheLimitFetchesNoFont)
{
    publish(QStringLiteral("a.ttf"), tinyFont(QStringLiteral("Fetcher Long Manifest")));
    publish(QStringLiteral("index.txt"), "a.ttf\n" + QByteArray(4096, '\n'));

    EXPECT_EQ(fetchAll().join(QLatin1Char(' ')).toStdString(), "fonts/index.txt");
    EXPECT_FALSE(QFontDatabase::families().contains(QStringLiteral("Fetcher Long Manifest")));
}

class FontChangeCounter : public QWidget
{
public:
    using QWidget::QWidget;
    int fontChanges = 0;

protected:
    void changeEvent(QEvent *event) override
    {
        fontChanges += event->type() == QEvent::FontChange;
        QWidget::changeEvent(event);
    }
};

TEST_F(FallbackFontFetcherTest, EveryWidgetIsToldThatTheFontsChanged)
{
    publish(QStringLiteral("a.ttf"), tinyFont(QStringLiteral("Fetcher Widgets")));
    publishManifest({QStringLiteral("a.ttf")});
    FontChangeCounter window;
    FontChangeCounter child(&window);

    fetchAll();

    EXPECT_EQ(window.fontChanges, 1);
    EXPECT_EQ(child.fontChanges, 1);
}

class PaintCounter : public QGraphicsRectItem
{
public:
    int paints = 0;

    void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        ++paints;
    }
};

// A cached item is drawn from its stored picture, which still holds the boxes of the missing glyphs.
TEST_F(FallbackFontFetcherTest, ACachedBoardItemIsPaintedAgain)
{
    publish(QStringLiteral("a.ttf"), tinyFont(QStringLiteral("Fetcher Board")));
    publishManifest({QStringLiteral("a.ttf")});
    QGraphicsScene scene;
    auto *item = new PaintCounter;
    item->setRect(0, 0, 20, 20);
    item->setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    scene.addItem(item);
    QGraphicsView view(&scene);
    view.show();
    ASSERT_TRUE(QTest::qWaitFor([item] { return item->paints > 0; }, 5000));
    const int before = item->paints;

    fetchAll();

    EXPECT_TRUE(QTest::qWaitFor([item, before] { return item->paints > before; }, 2000));
}

TEST_F(FallbackFontFetcherTest, TheGeneratedFontsAreAccepted)
{
    EXPECT_GE(QFontDatabase::addApplicationFontFromData(tinyFont(QStringLiteral("Fetcher Fixture"))), 0);
    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher Fixture")));
    EXPECT_GE(QFontDatabase::addApplicationFontFromData(
                  tinyFont(QStringLiteral("Fetcher Large Fixture"), FallbackFontFetcher::maxFontBytes)),
              0);
    EXPECT_TRUE(QFontDatabase::families().contains(QStringLiteral("Fetcher Large Fixture")));
}
} // namespace

int main(int argc, char **argv)
{
    return DialogTest::run(argc, argv, QStringLiteral("fallback-font-fetcher-test"));
}

#include "dlg_load_deck_from_website.h"

#include "../../deck_loader/card_node_function.h"
#include "../utility/non_blocking_dialogs.h"
#include "parsers/deck_link_to_api_transformer.h"
#include "parsers/interface_json_deck_parser.h"

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QDebug>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMessageLogger>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <libcockatrice/card/import/card_name_normalizer.h>
#include <libcockatrice/network/network_manager.h>
#include <qnamespace.h>
#include <version_string.h>

class QWidget;

static const QStringList SUPPORTED_PROVIDERS = {"Archidekt", "Deckstats", "ManaBox", "Moxfield", "TappedOut"};
DlgLoadDeckFromWebsite::DlgLoadDeckFromWebsite(QWidget *parent, QNetworkAccessManager *networkManager)
    : QDialog(parent), nam(networkManager ? networkManager : NetworkManagerFactory::create(this))
{
    layout = new QVBoxLayout(this);
    setLayout(layout);

    instructionLabel = new QLabel(this);
    instructionLabel->setWordWrap(true);
    layout->addWidget(instructionLabel);

    urlEdit = new QLineEdit(this);
    urlEdit->setText(QApplication::clipboard()->text());

    layout->addWidget(urlEdit);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    okButton = buttonBox->button(QDialogButtonBox::Ok);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(buttonBox);

    if (testValidUrl()) {
        QMetaObject::invokeMethod(this, "accept", Qt::QueuedConnection);
        hide();
    }

    retranslateUi();
}

void DlgLoadDeckFromWebsite::retranslateUi()
{
    instructionLabel->setText(tr("Paste a link to a decklist site here to import it.") + "\n" +
                              tr("Supported services: %1").arg(SUPPORTED_PROVIDERS.join(", ")));
}

bool DlgLoadDeckFromWebsite::testValidUrl()
{
    ParsedDeckInfo info;
    return DeckLinkToApiTransformer::parseDeckUrl(urlEdit->text(), info);
}

void DlgLoadDeckFromWebsite::warn(const QString &message)
{
    Dialogs::warning(this, tr("Load Deck from Website"), message);
}

void DlgLoadDeckFromWebsite::accept()
{
    // accept() is a public slot: a call while a request runs must not start a second one.
    if (!okButton->isEnabled()) {
        return;
    }

    ParsedDeckInfo info;
    if (DeckLinkToApiTransformer::parseDeckUrl(urlEdit->text(), info)) {
        qCInfo(DlgLoadDeckFromWebsiteLog) << info.baseUrl << info.deckID << info.fullUrl;

        if (!createParserForProvider(info.provider) && info.provider != DeckProvider::Deckstats &&
            info.provider != DeckProvider::TappedOut) {
            qCWarning(DlgLoadDeckFromWebsiteLog) << "No parser found for provider";
            warn(tr("No parser available for this deck provider.") + "\n" +
                 tr("Supported services: %1").arg(SUPPORTED_PROVIDERS.join(", ")));
            return;
        }

        QNetworkRequest request(QUrl(info.fullUrl));
        request.setHeader(QNetworkRequest::UserAgentHeader, QString("Cockatrice %1").arg(VERSION_STRING));
        QNetworkReply *reply = nam->get(request);
        okButton->setEnabled(false);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, provider = info.provider] { readReply(reply, provider); });

    } else {
        qCInfo(DlgLoadDeckFromWebsiteLog) << "URL not recognized";
        warn(tr("The provided URL is not recognized as a valid deck URL.\n"
                "Valid deck URLs look like this:\n\n"
                "https://archidekt.com/decks/9999999\n"
                "https://deckstats.net/decks/99999/9999999-your-deck-name/en\n"
                "https://moxfield.com/decks/XYZxx-XYZ99Yyy-xyzXzzz\n"
                "https://manabox.app/decks/XYZxxa-YZ99Yyyx_yzXzzz\n"
                "https://tappedout.net/mtg-decks/your-deck-name/"));
    }
}

void DlgLoadDeckFromWebsite::readReply(QNetworkReply *reply, DeckProvider provider)
{
    okButton->setEnabled(true);
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(DlgLoadDeckFromWebsiteLog) << "Network error:" << reply->errorString();
        warn(tr("Network error: %1").arg(reply->errorString()));
        return;
    }

    QByteArray responseData = reply->readAll();

    // Special handling for Deckstats and TappedOut .txt
    if (provider == DeckProvider::Deckstats || provider == DeckProvider::TappedOut) {
        QString deckText = QString::fromUtf8(responseData);
        if (deckText.isEmpty()) {
            qCWarning(DlgLoadDeckFromWebsiteLog) << "Response is empty";
            warn(tr("Received empty deck data."));
            return;
        }

        // Parse the plain text deck here
        DeckList deckList;
        QTextStream stream(&deckText);
        deckList.loadFromStream_Plain(stream, false, CardNameNormalizer());
        deckList.forEachCard(CardNodeFunction::ResolveProviderId());
        deck = deckList;

        QDialog::accept();
        return;
    }

    // Normal JSON parsing for other providers
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(DlgLoadDeckFromWebsiteLog) << "JSON parse error:" << parseError.errorString();
        warn(tr("Failed to parse deck data: %1").arg(parseError.errorString()));
        return;
    }

    deck = createParserForProvider(provider)->parse(doc.object());
    QDialog::accept();
}

QSharedPointer<IJsonDeckParser> DlgLoadDeckFromWebsite::createParserForProvider(DeckProvider provider)
{
    switch (provider) {
        case DeckProvider::Archidekt:
            return QSharedPointer<IJsonDeckParser>(new ArchidektJsonParser());
        case DeckProvider::Moxfield:
            return QSharedPointer<IJsonDeckParser>(new MoxfieldJsonParser());
        case DeckProvider::Manabox:
            return QSharedPointer<IJsonDeckParser>(new ManaboxJsonParser());
        default:
            return QSharedPointer<IJsonDeckParser>(nullptr);
    }
}

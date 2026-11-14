/**
 * @file dlg_load_deck_from_website.h
 * @ingroup RemoteDeckStorageDialogs
 * @ingroup Lobby
 */
//! \todo Document this file.

#ifndef DLG_LOAD_DECK_FROM_WEBSITE_H
#define DLG_LOAD_DECK_FROM_WEBSITE_H

#include "../client/network/parsers/interface_json_deck_parser.h"
#include "libcockatrice/deck_list/deck_list.h"

#include <QDialog>
#include <QLoggingCategory>
#include <QSharedPointer>
#include <qtmetamacros.h>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QVBoxLayout;
class QWidget;
enum class DeckProvider;
template <class T> class QSharedPointer;

inline Q_LOGGING_CATEGORY(DlgLoadDeckFromWebsiteLog, "dlg_load_deck_from_website");

class DlgLoadDeckFromWebsite : public QDialog
{
    Q_OBJECT
public:
    explicit DlgLoadDeckFromWebsite(QWidget *parent, QNetworkAccessManager *networkManager = nullptr);
    void retranslateUi();
    bool testValidUrl();
    DeckList deck;

    const DeckList &getDeck() const
    {
        return deck;
    }

private:
    QNetworkAccessManager *nam;
    QVBoxLayout *layout;
    QLabel *instructionLabel;
    QLineEdit *urlEdit;
    QPushButton *okButton;

    void warn(const QString &message);
    void readReply(QNetworkReply *reply, DeckProvider provider);

public slots:
    void accept() override;
    QSharedPointer<IJsonDeckParser> createParserForProvider(DeckProvider provider);
};

#endif // DLG_LOAD_DECK_FROM_WEBSITE_H

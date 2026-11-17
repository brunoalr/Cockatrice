#include "dlg_load_deck.h"

#include "../../../client/settings/cache_settings.h"
#include "../../deck_loader/deck_loader.h"
#include "../utility/non_blocking_dialogs.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <libcockatrice/settings/paths_settings.h>
#include <libcockatrice/settings/recents_settings.h>

class QWidget;

DlgLoadDeck::DlgLoadDeck(QWidget *parent) : QFileDialog(parent, tr("Load Deck"))
{
    QString startingDir = SettingsCache::instance().recents().getLatestDeckDirPath();
    if (startingDir.isEmpty()) {
        startingDir = SettingsCache::instance().paths().getDeckPath();
    }

    setDirectory(startingDir);
    setNameFilters(DeckLoader::FILE_NAME_FILTERS);

    connect(this, &DlgLoadDeck::accepted, this, &DlgLoadDeck::actAccepted);
}

void DlgLoadDeck::choose(QWidget *parent, std::function<void(const QString &fileName)> thenChosen)
{
    if (SettingsCache::instance().canReachDeviceFolders()) {
        auto *dialog = new DlgLoadDeck(parent);
        Dialogs::open(dialog, [dialog, thenChosen = std::move(thenChosen)](int result) {
            if (result == QDialog::Accepted) {
                thenChosen(dialog->selectedFiles().at(0));
            }
        });
        return;
    }

    const QDir folder(SettingsCache::instance().paths().getDeckPath());
    QStringList decks;
    QDirIterator found(folder.path(), DeckLoader::ACCEPTED_FILE_EXTENSIONS, QDir::Files, QDirIterator::Subdirectories);
    while (found.hasNext()) {
        decks.append(folder.relativeFilePath(found.next()));
    }
    if (decks.isEmpty()) {
        Dialogs::information(parent, tr("Load Deck"),
                             tr("No deck is saved in this browser yet. Build one in the deck editor and save it, "
                                "import one from the device, or load one from the clipboard or from an online "
                                "service."));
        return;
    }
    decks.sort(Qt::CaseInsensitive);

    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(tr("Load Deck"));
    auto *filter = new QLineEdit(dialog);
    filter->setPlaceholderText(tr("Filter decks"));
    filter->setClearButtonEnabled(true);
    auto *list = new QListWidget(dialog);
    list->addItems(decks);
    list->setCurrentRow(0);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(filter);
    layout->addWidget(list);
    layout->addWidget(buttons);

    connect(filter, &QLineEdit::textChanged, list, [list, ok](const QString &text) {
        QListWidgetItem *firstShown = nullptr;
        for (int row = 0; row < list->count(); ++row) {
            QListWidgetItem *item = list->item(row);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
            if (!firstShown && !item->isHidden()) {
                firstShown = item;
            }
        }
        if (!list->currentItem() || list->currentItem()->isHidden()) {
            list->setCurrentItem(firstShown);
        }
        ok->setEnabled(firstShown != nullptr);
    });
    connect(list, &QListWidget::itemDoubleClicked, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    Dialogs::open(dialog, [list, folder, thenChosen = std::move(thenChosen)](int result) {
        if (result == QDialog::Accepted && list->currentItem()) {
            thenChosen(folder.filePath(list->currentItem()->text()));
        }
    });
}

void DlgLoadDeck::actAccepted()
{
    SettingsCache::instance().recents().setLatestDeckDirPath(directory().absolutePath());
}
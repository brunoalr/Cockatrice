#include "dlg_convert_deck_to_cod_format.h"

#include "../../../client/settings/cache_settings.h"
#include "../utility/non_blocking_dialogs.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QObject>
#include <QVBoxLayout>
#include <libcockatrice/deck_list/deck_file_format.h>
#include <libcockatrice/settings/visual_deck_storage_settings.h>

DialogConvertDeckToCodFormat::DialogConvertDeckToCodFormat(QWidget *parent) : QDialog(parent)
{
    layout = new QVBoxLayout(this);
    label = new QLabel();
    layout->addWidget(label);

    dontAskAgainCheckbox = new QCheckBox(this);
    layout->addWidget(dontAskAgainCheckbox);

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() { accept(); });

    connect(buttonBox, &QDialogButtonBox::rejected, this, [this]() { reject(); });

    setLayout(layout);
    retranslateUi();
}

void DialogConvertDeckToCodFormat::retranslateUi()
{
    setWindowTitle(tr("Deck Format Conversion"));
    label->setText(
        tr("You tried to add a tag to a .txt format deck.\n Tags can only be added to .cod format decks.\n Do "
           "you want to convert the deck to the .cod format?"));
    dontAskAgainCheckbox->setText(tr("Remember and automatically apply choice in the future"));
}

bool DialogConvertDeckToCodFormat::dontAskAgain() const
{
    return dontAskAgainCheckbox->isChecked();
}

namespace
{

void confirmOverwriteIfExists(QWidget *parent, const QString &filePath, std::function<void(bool overwrite)> then)
{
    QFileInfo fileInfo(filePath);
    QString newFileName = QDir::toNativeSeparators(fileInfo.path() + "/" + fileInfo.completeBaseName() + ".cod");

    if (QFile::exists(newFileName)) {
        Dialogs::confirm(parent, QObject::tr("Overwrite Existing File?"),
                         QObject::tr("A .cod version of this deck already exists. Overwrite it?"), std::move(then));
        return;
    }
    then(true);
}

void convertAccepted(QWidget *parent,
                     const QString &filePath,
                     const std::function<bool()> &convert,
                     bool dontAskAgain,
                     const std::function<void(bool convertible)> &then)
{
    confirmOverwriteIfExists(parent, filePath, [convert, dontAskAgain, then](bool overwrite) {
        if (!overwrite || !convert()) {
            then(false);
            return;
        }

        if (dontAskAgain) {
            SettingsCache::instance().visualDeckStorage().setVisualDeckStoragePromptForConversion(false);
            SettingsCache::instance().visualDeckStorage().setVisualDeckStorageAlwaysConvert(true);
        }

        then(true);
    });
}

} // namespace

void DialogConvertDeckToCodFormat::promptIfRequired(QWidget *parent,
                                                    const QString &filePath,
                                                    const std::function<bool()> &convert,
                                                    std::function<void(bool convertible)> then)
{
    if (DeckFileFormat::getFormatFromName(filePath) == DeckFileFormat::Cockatrice) {
        then(true);
        return;
    }

    // Retrieve saved preference if the prompt is disabled
    if (!SettingsCache::instance().visualDeckStorage().getVisualDeckStoragePromptForConversion()) {
        if (!SettingsCache::instance().visualDeckStorage().getVisualDeckStorageAlwaysConvert()) {
            then(false);
            return;
        }

        confirmOverwriteIfExists(parent, filePath, [convert, then](bool overwrite) { then(overwrite && convert()); });
        return;
    }

    // Show the dialog to the user
    auto *conversionDialog = new DialogConvertDeckToCodFormat(parent);
    Dialogs::open(conversionDialog, [conversionDialog, parent, filePath, convert, then](int result) {
        if (result != QDialog::Accepted) {
            SettingsCache::instance().visualDeckStorage().setVisualDeckStoragePromptForConversion(
                !conversionDialog->dontAskAgain());
            SettingsCache::instance().visualDeckStorage().setVisualDeckStorageAlwaysConvert(false);

            then(false);
            return;
        }

        convertAccepted(parent, filePath, convert, conversionDialog->dontAskAgain(), then);
    });
}

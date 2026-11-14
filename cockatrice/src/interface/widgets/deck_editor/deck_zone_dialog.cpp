#include "deck_zone_dialog.h"

#include "../utility/non_blocking_dialogs.h"
#include "deck_state_manager.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>
#include <libcockatrice/deck_list/tree/inner_deck_list_node.h>
#include <libcockatrice/utility/string_limits.h>

DeckZoneDialog::DeckZoneDialog(QWidget *parent,
                               const QString &initialBoardName,
                               const std::function<QString(const QString &)> &_nameValidator,
                               bool _allowBoardSelection)
    : QDialog(parent), nameValidator(_nameValidator), allowBoardSelection(_allowBoardSelection)
{
    nameLabel = new QLabel(this);
    nameEdit = new QLineEdit(this);
    nameEdit->setMaxLength(MAX_NAME_LENGTH);

    errorLabel = new QLabel(this);
    errorLabel->hide();

    boardLabel = new QLabel(this);
    boardCombo = new QComboBox(this);
    for (const QString &boardName : InnerDecklistNode::boardZoneNames()) {
        // Use the icon overload explicitly so `boardName` lands in the user data role
        // (visible text is applied below in retranslateUi). The two-argument form
        // addItem({}, boardName) would be ambiguous and resolve to the icon overload
        // with empty user data, yielding empty entries and an empty getBoardName().
        boardCombo->addItem({}, {}, boardName);
    }
    if (!initialBoardName.isEmpty()) {
        int idx = boardCombo->findData(initialBoardName);
        if (idx != -1) {
            boardCombo->setCurrentIndex(idx);
        }
    }

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(nameLabel);
    layout->addWidget(nameEdit);
    layout->addWidget(errorLabel);
    if (allowBoardSelection) {
        layout->addWidget(boardLabel);
        layout->addWidget(boardCombo);
    } else {
        boardLabel->hide();
        boardCombo->hide();
    }
    layout->addWidget(buttonBox);

    retranslateUi();

    connect(nameEdit, &QLineEdit::textChanged, this, [this] { validateName(); });
    validateName();

    nameEdit->setFocus();
}

QString DeckZoneDialog::getZoneName() const
{
    return nameEdit->text().trimmed();
}

QString DeckZoneDialog::getBoardName() const
{
    return boardCombo->currentData().toString();
}

void DeckZoneDialog::setZoneName(const QString &zoneName)
{
    nameEdit->setText(zoneName);
    nameEdit->selectAll();
}

void DeckZoneDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);

    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void DeckZoneDialog::retranslateUi()
{
    setWindowTitle(allowBoardSelection ? tr("New zone") : tr("Rename zone"));

    nameLabel->setText(tr("Zone &name:"));
    nameLabel->setBuddy(nameEdit);

    boardLabel->setText(tr("&Parent zone:"));
    boardLabel->setBuddy(boardCombo);

    for (int i = 0; i < boardCombo->count(); i++) {
        boardCombo->setItemText(i, InnerDecklistNode::visibleNameFromName(boardCombo->itemData(i).toString()));
    }
}

void DeckZoneDialog::validateName()
{
    const QString zoneName = nameEdit->text().trimmed();
    QString error;
    if (zoneName.isEmpty()) {
        error = tr("Enter a zone name.");
    } else if (nameValidator) {
        error = nameValidator(zoneName);
    }

    errorLabel->setText(error);
    errorLabel->setVisible(!error.isEmpty());
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(error.isEmpty());
}

void DeckZoneDialog::promptForNewZone(
    QWidget *parent,
    const QString &initialBoardName,
    const std::function<QString(const QString &)> &nameValidator,
    std::function<void(const QString &boardName, const QString &zoneName)> thenAccepted)
{
    auto *dialog = new DeckZoneDialog(parent, initialBoardName, nameValidator);
    Dialogs::open(dialog, [dialog, thenAccepted = std::move(thenAccepted)](int result) {
        if (result == QDialog::Accepted) {
            thenAccepted(dialog->getBoardName(), dialog->getZoneName());
        }
    });
}

void DeckZoneDialog::promptToCreateZone(QWidget *parent,
                                        DeckStateManager *deck,
                                        const QString &initialBoardName,
                                        std::function<void(const QString &zoneName)> thenCreated)
{
    promptForNewZone(
        parent, initialBoardName, [deck](const QString &candidate) { return deck->validateNewZoneName(candidate); },
        [deck, thenCreated = std::move(thenCreated)](const QString &boardName, const QString &zoneName) {
            deck->createCustomZone(boardName, zoneName);
            if (thenCreated) {
                thenCreated(zoneName);
            }
        });
}

void DeckZoneDialog::promptForRename(QWidget *parent,
                                     const QString &currentZoneName,
                                     const std::function<QString(const QString &)> &nameValidator,
                                     std::function<void(const QString &newName)> thenAccepted)
{
    auto *dialog = new DeckZoneDialog(parent, {}, nameValidator, false);
    dialog->setZoneName(currentZoneName);
    Dialogs::open(dialog, [dialog, thenAccepted = std::move(thenAccepted)](int result) {
        if (result == QDialog::Accepted) {
            thenAccepted(dialog->getZoneName());
        }
    });
}

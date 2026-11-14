/**
 * @file deck_zone_dialog.h
 * @ingroup DeckEditorWidgets
 * @brief Shared dialog for creating custom deck zones.
 */

#ifndef DECK_ZONE_DIALOG_H
#define DECK_ZONE_DIALOG_H

#include <QDialog>
#include <QString>
#include <functional>
#include <qtmetamacros.h>

class DeckStateManager;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QWidget;

/**
 * @brief Modal dialog asking for the name and parent zone of a new custom deck zone.
 *
 * Each prompt constructs a new dialog, so validation state only ever reflects the
 * name currently typed.
 */
class DeckZoneDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the dialog and runs the initial validation pass.
     *
     * @param parent The parent widget for the dialog
     * @param initialBoardName The board zone to preselect in the combo. Unknown names
     *        fall back to main.
     * @param _nameValidator Given the trimmed candidate name, returns an empty string
     *        when it is usable, otherwise a user-facing error message. May be empty.
     * @param _allowBoardSelection When false the parent-zone combo is hidden and the
     *        dialog acts as a rename prompt for an existing zone.
     */
    explicit DeckZoneDialog(QWidget *parent = nullptr,
                            const QString &initialBoardName = {},
                            const std::function<QString(const QString &)> &_nameValidator = {},
                            bool _allowBoardSelection = true);

    /**
     * @brief The trimmed zone name entered by the user.
     */
    [[nodiscard]] QString getZoneName() const;

    /**
     * @brief The internal name of the board zone selected in the combo.
     */
    [[nodiscard]] QString getBoardName() const;

    /**
     * @brief Prefills the name field, e.g. with the current name when renaming.
     *
     * @param zoneName The text to put into the name field, selected for quick editing
     */
    void setZoneName(const QString &zoneName);

    /**
     * @brief Prompts the user for a new custom zone name and the board zone to nest it under.
     *
     * Returns at once; the dialog is application-modal.
     *
     * @param parent The parent widget for the dialog
     * @param initialBoardName The board zone to preselect in the dialog. Unknown names fall
     *        back to main.
     * @param nameValidator Optional validator forwarded to the dialog
     * @param thenAccepted Called with the chosen board zone and the trimmed zone name once
     *        the user accepts; not called when the user cancels
     */
    static void promptForNewZone(QWidget *parent,
                                 const QString &initialBoardName,
                                 const std::function<QString(const QString &)> &nameValidator,
                                 std::function<void(const QString &boardName, const QString &zoneName)> thenAccepted);

    /**
     * @brief Prompts for a new custom zone and creates it in @p deck once the user accepts.
     *
     * @param thenCreated Called with the name of the created zone; not called when the user cancels
     */
    static void promptToCreateZone(QWidget *parent,
                                   DeckStateManager *deck,
                                   const QString &initialBoardName,
                                   std::function<void(const QString &zoneName)> thenCreated = {});

    /**
     * @brief Prompts the user for a new name for an existing custom zone.
     *
     * Same inline validation as promptForNewZone, but without a parent-zone picker.
     *
     * @param parent The parent widget for the dialog
     * @param currentZoneName The current name, prefilled for editing
     * @param nameValidator Validator deciding whether a candidate name is usable. It sees
     *        the current name too, so callers wanting to allow unchanged names must
     *        special-case that themselves.
     * @param thenAccepted Called with the trimmed new name once the user accepts; not called
     *        when the user cancels
     */
    static void promptForRename(QWidget *parent,
                                const QString &currentZoneName,
                                const std::function<QString(const QString &)> &nameValidator,
                                std::function<void(const QString &newName)> thenAccepted);

protected:
    void changeEvent(QEvent *event) override;

private:
    /**
     * @brief Sets every user-visible string. Runs on construction and on runtime
     * language changes.
     */
    void retranslateUi();

    /**
     * @brief Validates the current input, toggling Ok and the inline error label.
     */
    void validateName();

    QLabel *nameLabel;
    QLineEdit *nameEdit;
    QLabel *errorLabel;
    QLabel *boardLabel;
    QComboBox *boardCombo;
    QDialogButtonBox *buttonBox;
    std::function<QString(const QString &)> nameValidator;
    bool allowBoardSelection;
};

#endif // DECK_ZONE_DIALOG_H

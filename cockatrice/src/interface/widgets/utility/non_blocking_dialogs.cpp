#include "non_blocking_dialogs.h"

#include <QInputDialog>
#include <QPointer>

namespace Dialogs
{
namespace
{
// finished() is emitted from inside the dialog's own close handling: a callback that destroys the parent
// there frees the dialog under Qt, and a QMessageBox has not recorded its escape button yet.
void show(QDialog *dialog, std::function<void(int result)> thenFinished)
{
    // Qt would delete the dialog before the queued callback; exec() holds this attribute back the same way.
    dialog->setAttribute(Qt::WA_DeleteOnClose, false);
    QObject::connect(
        dialog, &QDialog::finished, dialog,
        [dialog, thenFinished = std::move(thenFinished)](int result) {
            // Queued to the dialog, the call is dropped when the dialog dies first, with its parent or alone.
            QMetaObject::invokeMethod(
                dialog,
                [guard = QPointer<QDialog>(dialog), thenFinished, result] {
                    if (thenFinished) {
                        thenFinished(result);
                    }
                    // The callback may have opened the dialog again, for example after invalid input.
                    if (guard && !guard->isVisible()) {
                        guard->deleteLater();
                    }
                },
                Qt::QueuedConnection);
        },
        Qt::SingleShotConnection);
    if (dialog->parentWidget()) {
        dialog->open();
    } else {
        // open() would make a dialog without a parent modeless, where exec() blocks the application.
        dialog->setWindowModality(Qt::ApplicationModal);
        dialog->show();
    }
}

void notify(QWidget *parent,
            QMessageBox::Icon icon,
            const QString &title,
            const QString &text,
            std::function<void()> thenClosed)
{
    show(new QMessageBox(icon, title, text, QMessageBox::Ok, parent), [thenClosed = std::move(thenClosed)](int) {
        if (thenClosed) {
            thenClosed();
        }
    });
}

template <typename Dialog, typename Read, typename Callback>
void openForValue(Dialog *dialog, Read read, Callback thenAccepted)
{
    show(dialog, [dialog, read, thenAccepted = std::move(thenAccepted)](int result) {
        if (result == QDialog::Accepted && thenAccepted) {
            thenAccepted((dialog->*read)());
        }
    });
}

QInputDialog *inputDialog(QWidget *parent, const QString &title, const QString &label)
{
    auto *dialog = new QInputDialog(parent);
    dialog->setWindowTitle(title);
    dialog->setLabelText(label);
    return dialog;
}
} // namespace

void information(QWidget *parent, const QString &title, const QString &text, std::function<void()> thenClosed)
{
    notify(parent, QMessageBox::Information, title, text, std::move(thenClosed));
}

void warning(QWidget *parent, const QString &title, const QString &text, std::function<void()> thenClosed)
{
    notify(parent, QMessageBox::Warning, title, text, std::move(thenClosed));
}

void critical(QWidget *parent, const QString &title, const QString &text, std::function<void()> thenClosed)
{
    notify(parent, QMessageBox::Critical, title, text, std::move(thenClosed));
}

void confirm(QWidget *parent,
             const QString &title,
             const QString &text,
             std::function<void(bool confirmed)> then,
             QMessageBox::StandardButton defaultButton)
{
    ask(parent, QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, defaultButton,
        [then = std::move(then)](QMessageBox::StandardButton answer) {
            if (then) {
                then(answer == QMessageBox::Yes);
            }
        });
}

void ask(QWidget *parent,
         QMessageBox::Icon icon,
         const QString &title,
         const QString &text,
         QMessageBox::StandardButtons buttons,
         QMessageBox::StandardButton defaultButton,
         std::function<void(QMessageBox::StandardButton answer)> then)
{
    auto *box = new QMessageBox(icon, title, text, buttons, parent);
    // The static QMessageBox functions default to the first AcceptRole button when given NoButton.
    if (defaultButton == QMessageBox::NoButton) {
        for (uint mask = QMessageBox::FirstButton; mask <= QMessageBox::LastButton; mask <<= 1) {
            QAbstractButton *button = box->button(QMessageBox::StandardButton(mask));
            if (button && box->buttonRole(button) == QMessageBox::AcceptRole) {
                defaultButton = QMessageBox::StandardButton(mask);
                break;
            }
        }
    }
    box->setDefaultButton(defaultButton);
    show(box, [box, then = std::move(then)](int) {
        if (then) {
            then(box->standardButton(box->clickedButton()));
        }
    });
}

void getText(QWidget *parent,
             const QString &title,
             const QString &label,
             const QString &text,
             std::function<void(const QString &text)> thenAccepted,
             QLineEdit::EchoMode echo)
{
    auto *dialog = inputDialog(parent, title, label);
    dialog->setTextValue(text);
    dialog->setTextEchoMode(echo);
    openForValue(dialog, &QInputDialog::textValue, std::move(thenAccepted));
}

void getMultiLineText(QWidget *parent,
                      const QString &title,
                      const QString &label,
                      const QString &text,
                      std::function<void(const QString &text)> thenAccepted)
{
    auto *dialog = inputDialog(parent, title, label);
    dialog->setOptions(QInputDialog::UsePlainTextEditForTextInput);
    dialog->setTextValue(text);
    openForValue(dialog, &QInputDialog::textValue, std::move(thenAccepted));
}

void getInt(QWidget *parent,
            const QString &title,
            const QString &label,
            int value,
            int minimum,
            int maximum,
            std::function<void(int value)> thenAccepted)
{
    auto *dialog = inputDialog(parent, title, label);
    dialog->setIntRange(minimum, maximum);
    dialog->setIntValue(value);
    openForValue(dialog, &QInputDialog::intValue, std::move(thenAccepted));
}

void getColor(const QColor &initial,
              QWidget *parent,
              const QString &title,
              std::function<void(const QColor &color)> thenChosen,
              QColorDialog::ColorDialogOptions options)
{
    auto *dialog = new QColorDialog(parent);
    dialog->setOptions(options);
    if (!title.isEmpty()) {
        dialog->setWindowTitle(title);
    }
    dialog->setCurrentColor(initial);
    openForValue(dialog, &QColorDialog::selectedColor, std::move(thenChosen));
}

void open(QMessageBox *box, std::function<void(QAbstractButton *clicked)> thenClicked)
{
    show(box, [box, thenClicked = std::move(thenClicked)](int) {
        if (thenClicked) {
            thenClicked(box->clickedButton());
        }
    });
}

void detail::openThen(QDialog *dialog, std::function<void(int result)> thenFinished)
{
    show(dialog, std::move(thenFinished));
}
} // namespace Dialogs

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

#include "non_blocking_dialogs.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QScreen>
#include <QToolButton>
#include <QWindow>
#include <algorithm>

namespace Dialogs
{
namespace
{
// A dialog whose closeEvent() skips QDialog's is hidden without finished(): finish it, or it is never reported.
class FinishWhenHidden : public QObject
{
public:
    explicit FinishWhenHidden(QDialog *_dialog) : QObject(_dialog), dialog(_dialog)
    {
        dialog->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Hide && !event->spontaneous()) {
            QMetaObject::invokeMethod(
                this,
                [this] {
                    if (!dialog->isVisible()) {
                        dialog->done(dialog->result());
                    }
                },
                Qt::QueuedConnection);
        }
        return false;
    }

private:
    QDialog *dialog;
};

// The browser activates a window before its widgets are visible, and Qt then gives the focus to the widget after
// the one the dialog chose. A QInputDialog chooses only as it is shown, so the choice is read at the activation.
class FocusAtActivation : public QObject
{
public:
    explicit FocusAtActivation(QDialog *dialog) : chosen(dialog->focusWidget())
    {
        dialog->installEventFilter(this);
    }

    QPointer<QWidget> chosen;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::WindowActivate) {
            chosen = static_cast<QWidget *>(watched)->focusWidget();
        }
        return false;
    }
};

QWidget *openerOf(const QWidget *popup)
{
    if (popup->parentWidget()) {
        return popup->parentWidget()->window();
    }
    const QWindow *opener = popup->windowHandle() ? popup->windowHandle()->transientParent() : nullptr;
    const QWidgetList windows = QApplication::topLevelWidgets();
    const auto it = std::ranges::find_if(
        windows, [opener](const QWidget *window) { return opener && window->windowHandle() == opener; });
    return it == windows.end() ? nullptr : *it;
}

// Qt closes a popup as a dialog shows, and a browser keeps a dialog above the window it belongs to only:
// one of the closed popup sinks under the window raised next. It still goes with the widget that asked.
void moveOutOfPopup(QDialog *dialog)
{
    QWidget *asker = dialog->parentWidget();
    QWidget *owner = asker ? asker->window() : nullptr;
    if (!owner || owner->windowType() != Qt::Popup) {
        return;
    }
    while (owner && owner->windowType() == Qt::Popup) {
        owner = openerOf(owner);
    }
    dialog->setParent(owner, dialog->windowFlags());
    QObject::connect(asker, &QObject::destroyed, dialog, [dialog] { delete dialog; });
}

// finished() is emitted from inside the dialog's own close handling: a callback that destroys the parent
// there frees the dialog under Qt, and a QMessageBox has not recorded its escape button yet.
void show(QDialog *dialog, std::function<void(int result)> thenFinished)
{
    moveOutOfPopup(dialog);
    // Qt would delete the dialog before the queued callback; exec() holds this attribute back the same way.
    dialog->setAttribute(Qt::WA_DeleteOnClose, false);
    auto *hidden = new FinishWhenHidden(dialog);
    QObject::connect(
        dialog, &QDialog::finished, dialog,
        [dialog, hidden, thenFinished = std::move(thenFinished)](int result) {
            delete hidden; // NOSONAR: a child of the dialog, done with its one job
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // The platform runs a native box its own way: KDE's never finishes when it is closed from its title bar,
    // and macOS runs one in a nested loop that outlives a box destroyed with its parent.
    if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
        box->setOption(QMessageBox::Option::DontUseNativeDialog);
    }
#endif
    // As exec() does: open() would block the parent window only and make the dialog a sheet on macOS.
    // A file dialog must be that sheet: macOS defers an application-modal native one to an exec() that never comes.
    const bool isFileDialog = qobject_cast<QFileDialog *>(dialog) != nullptr;
    dialog->setWindowModality(isFileDialog ? Qt::WindowModal : Qt::ApplicationModal);
    FocusAtActivation focus(dialog);
    dialog->show();
    if (focus.chosen) {
        focus.chosen->setFocus();
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

void popUpUnder(QAbstractButton *button, QMenu *menu)
{
    // The press that closes the menu on its button would otherwise reach the button and open it again.
    menu->setNoReplayFor(button);
    const QSize size = menu->sizeHint();
    const QRect area = button->screen()->availableGeometry();
    const bool fitsBelow =
        button->mapToGlobal(QPoint(0, button->height())).y() + size.height() <= area.y() + area.height();
    const int x = button->isRightToLeft() ? button->width() - size.width() : 0;
    menu->popup(button->mapToGlobal(QPoint(x, fitsBelow ? button->height() : -size.height())));
}

// Taking the press keeps the button from showing the menu itself.
class ToolButtonMenuPopper : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick) {
            return false;
        }
        auto *button = qobject_cast<QToolButton *>(watched);
        if (!button || !button->isEnabled() || button->popupMode() != QToolButton::InstantPopup || !button->menu() ||
            static_cast<QMouseEvent *>(event)->button() != Qt::LeftButton) {
            return false;
        }
        popUpUnder(button, button->menu());
        return true;
    }
};
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
#ifdef Q_OS_WASM
    // A page cannot read the screen: the picked colour would be meaningless.
    options |= QColorDialog::NoEyeDropperButton;
#endif
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

void showMenuOnClick(QAbstractButton *button, QMenu *menu)
{
    QObject::connect(button, &QAbstractButton::clicked, menu, [button, menu] { popUpUnder(button, menu); });
}

void popUpToolButtonMenus(QObject *scope)
{
    scope->installEventFilter(new ToolButtonMenuPopper(scope));
}

void detail::openThen(QDialog *dialog, std::function<void(int result)> thenFinished)
{
    show(dialog, std::move(thenFinished));
}
} // namespace Dialogs

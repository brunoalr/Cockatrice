#ifndef NON_BLOCKING_DIALOGS_H
#define NON_BLOCKING_DIALOGS_H

#include <QColor>
#include <QColorDialog>
#include <QDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace Dialogs
{
void information(QWidget *parent, const QString &title, const QString &text, std::function<void()> thenClosed = {});
void warning(QWidget *parent, const QString &title, const QString &text, std::function<void()> thenClosed = {});
void critical(QWidget *parent, const QString &title, const QString &text, std::function<void()> thenClosed = {});

void confirm(QWidget *parent,
             const QString &title,
             const QString &text,
             std::function<void(bool confirmed)> then,
             QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

void ask(QWidget *parent,
         QMessageBox::Icon icon,
         const QString &title,
         const QString &text,
         QMessageBox::StandardButtons buttons,
         QMessageBox::StandardButton defaultButton,
         std::function<void(QMessageBox::StandardButton answer)> then);

void getText(QWidget *parent,
             const QString &title,
             const QString &label,
             const QString &text,
             std::function<void(const QString &text)> thenAccepted,
             QLineEdit::EchoMode echo = QLineEdit::Normal);
void getMultiLineText(QWidget *parent,
                      const QString &title,
                      const QString &label,
                      const QString &text,
                      std::function<void(const QString &text)> thenAccepted);
void getInt(QWidget *parent,
            const QString &title,
            const QString &label,
            int value,
            int minimum,
            int maximum,
            int step,
            std::function<void(int value)> thenAccepted);
void getItem(QWidget *parent,
             const QString &title,
             const QString &label,
             const QStringList &items,
             int current,
             bool editable,
             std::function<void(const QString &item)> thenAccepted);

void getColor(const QColor &initial,
              QWidget *parent,
              const QString &title,
              std::function<void(const QColor &color)> thenChosen,
              QColorDialog::ColorDialogOptions options = {});

namespace detail
{
void openThen(QDialog *dialog, std::function<void(int result)> thenFinished);
}

// Takes the dialog over: it must be allocated with new, and it is deleted after its callback unless the
// callback opened it again. A dialog kept for reuse must connect to finished() itself.
template <typename Dialog> void open(Dialog *dialog, std::function<void(int result)> thenFinished)
{
    static_assert(!std::is_base_of_v<QMessageBox, Dialog>,
                  "pass a QMessageBox to the overload that reports the clicked button");
    detail::openThen(dialog, std::move(thenFinished));
}

// Reports what QMessageBox::clickedButton() holds after exec(): the escape button for a window close.
void open(QMessageBox *box, std::function<void(QAbstractButton *clicked)> thenClicked);

template <typename Fn> auto whileAlive(QObject *context, Fn fn)
{
    return [guard = QPointer<QObject>(context), fn = std::move(fn)]<typename... Arguments>(Arguments &&...arguments)
        requires std::invocable<const Fn &, Arguments...>
    {
        if (guard) {
            fn(std::forward<Arguments>(arguments)...);
        }
    };
}
} // namespace Dialogs

#endif // NON_BLOCKING_DIALOGS_H

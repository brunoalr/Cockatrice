#include "get_text_with_max.h"

#include <QInputDialog>

class QWidget;

QInputDialog *newTextDialogWithMax(QWidget *parent,
                                   const QString &title,
                                   const QString &label,
                                   QLineEdit::EchoMode mode,
                                   const QString &text,
                                   int max,
                                   Qt::WindowFlags flags,
                                   Qt::InputMethodHints inputMethodHints)
{
    auto *dialog = new QInputDialog(parent, flags);
    dialog->setWindowTitle(title);
    dialog->setLabelText(label);
    dialog->setTextValue(text);
    dialog->setTextEchoMode(mode);
    dialog->setInputMethodHints(inputMethodHints);

    // find the qlineedit that this dialog holds, there should be only one
    dialog->findChild<QLineEdit *>()->setMaxLength(max);
    return dialog;
}

QString getTextWithMax(QWidget *parent,
                       const QString &title,
                       const QString &label,
                       QLineEdit::EchoMode mode,
                       const QString &text,
                       bool *ok,
                       int max,
                       Qt::WindowFlags flags,
                       Qt::InputMethodHints inputMethodHints)
{
    auto *dialog = newTextDialogWithMax(parent, title, label, mode, text, max, flags, inputMethodHints);

    const int ret = dialog->exec();
    if (ok != nullptr) {
        *ok = !!ret;
    }
    if (ret) {
        return dialog->textValue();
    } else {
        return QString();
    }
}

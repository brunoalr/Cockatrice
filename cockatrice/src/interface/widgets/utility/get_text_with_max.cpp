#include "get_text_with_max.h"

#include <QInputDialog>

class QWidget;

QInputDialog *newTextDialogWithMax(QWidget *parent,
                                   const QString &title,
                                   const QString &label,
                                   QLineEdit::EchoMode mode,
                                   const QString &text,
                                   int max)
{
    auto *dialog = new QInputDialog(parent);
    dialog->setWindowTitle(title);
    dialog->setLabelText(label);
    dialog->setTextValue(text);
    dialog->setTextEchoMode(mode);

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
                       int max)
{
    auto *dialog = newTextDialogWithMax(parent, title, label, mode, text, max);

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

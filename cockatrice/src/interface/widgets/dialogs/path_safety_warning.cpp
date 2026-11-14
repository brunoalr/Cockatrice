#include "path_safety_warning.h"

#include "../../client/settings/cache_settings.h"
#include "../utility/non_blocking_dialogs.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QObject>
#include <QString>
#include <libcockatrice/settings/paths_settings.h>

class QWidget;

bool PathSafetyWarning::confirmOutsideProgramDir(QWidget *parent, const QString &path)
{
    if (SettingsCache::instance().getIsPortableBuild()) {
        return true;
    }

    if (!PathsSettings::isInsideDir(path, QCoreApplication::applicationDirPath())) {
        return true;
    }

    auto *box = new QMessageBox(QMessageBox::Warning, QObject::tr("Path inside the program directory"),
                                QObject::tr("'%1' is inside the Cockatrice program directory.\n\n"
                                            "Files stored there can be deleted by an update or by uninstalling "
                                            "Cockatrice, so this change was not applied. Choose a location outside "
                                            "the program directory.")
                                    .arg(path),
                                QMessageBox::Ok, parent);
    box->setTextFormat(Qt::PlainText);
    Dialogs::open(box, {});

    return false;
}

void PathSafetyWarning::choosePath(QWidget *parent,
                                   const QString &title,
                                   QFileDialog::FileMode mode,
                                   const QString &current,
                                   std::function<void(const QString &path)> apply,
                                   const QString &fileInChosenDir)
{
    auto *dialog = new QFileDialog(parent, title, current);
    dialog->setFileMode(mode);
    if (mode == QFileDialog::Directory) {
        dialog->setOption(QFileDialog::ShowDirsOnly);
    }
    Dialogs::open(dialog, [parent, dialog, apply = std::move(apply), fileInChosenDir](int result) {
        if (result != QDialog::Accepted) {
            return;
        }
        QString path = dialog->selectedFiles().value(0);
        if (path.isEmpty()) {
            return;
        }
        if (!fileInChosenDir.isEmpty()) {
            path += "/" + fileInChosenDir;
        }
        if (confirmOutsideProgramDir(parent, path)) {
            apply(path);
        }
    });
}

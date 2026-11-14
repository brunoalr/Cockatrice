#ifndef COCKATRICE_PATH_SAFETY_WARNING_H
#define COCKATRICE_PATH_SAFETY_WARNING_H

#include <QFileDialog>
#include <QString>
#include <functional>

class QWidget;

namespace PathSafetyWarning
{

/**
 * @brief Warns when the user tries to configure a data path inside the program directory.
 *
 * Shows a message box explaining that files kept there can be deleted by an
 * update or by uninstalling Cockatrice. Portable builds are always exempt:
 * their data lives beside the executables by design.
 *
 * @param parent The parent widget
 * @param path The path the user is trying to configure
 * @return true when the path is outside the program directory and safe to apply
 */
bool confirmOutsideProgramDir(QWidget *parent, const QString &path);

void choosePath(QWidget *parent,
                const QString &title,
                QFileDialog::FileMode mode,
                const QString &current,
                std::function<void(const QString &path)> apply,
                const QString &fileInChosenDir = {});

} // namespace PathSafetyWarning

#endif // COCKATRICE_PATH_SAFETY_WARNING_H

/**
 * @file dlg_settings.cpp
 * @brief Implementation of the main settings dialog (thin frame around SettingsView)
 * @ingroup Dialogs
 */
#include "dlg_settings.h"

#include "../../../client/settings/cache_settings.h"
#include "../main.h"
#include "../settings_page/abstract_settings_page.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QSize>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>
#include <libcockatrice/settings/personal_settings.h>

static constexpr int MIN_DIALOG_WIDTH = 750;
static constexpr int MIN_DIALOG_HEIGHT = 700;
static constexpr int MAX_DIALOG_WIDTH = 1280;
static constexpr double DIALOG_SIZE_RATIO = 0.9;

/**
 * @brief Calculates the initial dialog size from the host window or screen
 * @param hostSize Size of the hosting main window, or empty when parentless
 * @param availableSize Available screen area the dialog must fit into
 * @param minimumSize Minimum dialog size already applied via setMinimumSize
 * @return Target size at ~90% of the host with small margins, clamped to fit
 */
static QSize calculateDialogSize(const QSize &hostSize, const QSize &availableSize, const QSize &minimumSize)
{
    QSize target = !hostSize.isEmpty() ? QSize(qRound(hostSize.width() * DIALOG_SIZE_RATIO),
                                               qRound(hostSize.height() * DIALOG_SIZE_RATIO))
                                       : QSize(qRound(availableSize.width() * DIALOG_SIZE_RATIO),
                                               qRound(availableSize.height() * DIALOG_SIZE_RATIO));
    // Evenly-spaced columns leave unnatural dead space on very wide windows, so clamp the width.
    target.setWidth(qMin(target.width(), MAX_DIALOG_WIDTH));
    return target.expandedTo(minimumSize).boundedTo(availableSize);
}

DlgSettings::DlgSettings(QWidget *parent) : QDialog(parent)
{
    // Fill the main window with small margins so multi-group pages (e.g. Deck Editor) are not buried in a scroll.
    // 90% of the host window leaves ~5% margins on each side; falls back to 90% of the screen when parentless.
    QWidget *hostWindow = parent ? parent->window() : nullptr;
    QScreen *screen = nullptr;
    QSize hostSize;
    if (hostWindow) {
        hostSize = hostWindow->size();
        screen = QGuiApplication::screenAt(hostWindow->geometry().center());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    const QSize minimum(qMin(MIN_DIALOG_WIDTH, available.width()), qMin(MIN_DIALOG_HEIGHT, available.height()));
    setMinimumSize(minimum);

    resize(calculateDialogSize(hostSize, available.size(), minimum));
    setSizeGripEnabled(true);

    setObjectName("settingsDialog");

    connect(&SettingsCache::instance().personal(), &PersonalSettings::langChanged, this, &DlgSettings::updateLanguage);

    settingsView = new SettingsView(this);

    // Bottom buttons
    auto *buttonBox = new QHBoxLayout;
    buttonBox->addStretch();
    okButton = new QPushButton;
    okButton->setDefault(true);
    okButton->setObjectName("settingsOkButton");
    connect(okButton, &QPushButton::clicked, this, &DlgSettings::close);
    buttonBox->addWidget(okButton);

    // Main layout
    auto *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(settingsView);
    mainLayout->addSpacing(4);
    mainLayout->addLayout(buttonBox);
    setLayout(mainLayout);

    connect(&SettingsCache::instance().personal(), &PersonalSettings::langChanged, this, &DlgSettings::retranslateUi);
    retranslateUi();
}

void DlgSettings::setTab(int index)
{
    settingsView->setTab(index);
}

AbstractSettingsPage *DlgSettings::page(SettingsPage which) const
{
    return settingsView->page(which);
}

void DlgSettings::updateLanguage()
{
    qApp->removeTranslator(translator); // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
    installNewTranslator();
}

void DlgSettings::retranslateUi()
{
    setWindowTitle(tr("Settings"));
    okButton->setText(tr("OK"));
}

#include "abstract_analytics_panel_widget.h"

#include "../../theme_icon.h"
#include "../general/display/banner_widget.h"
#include "../utility/non_blocking_dialogs.h"
#include "deck_list_statistics_analyzer.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringLiteral>
#include <QVBoxLayout>
#include <qnamespace.h>

AbstractAnalyticsPanelWidget::AbstractAnalyticsPanelWidget(QWidget *parent, DeckListStatisticsAnalyzer *analyzer)
    : QWidget(parent), analyzer(analyzer)
{
    layout = new QVBoxLayout(this);

    bannerAndSettingsContainer = new QWidget(this);

    bannerAndSettingsLayout = new QHBoxLayout(bannerAndSettingsContainer);
    bannerAndSettingsContainer->setLayout(bannerAndSettingsLayout);
    bannerWidget = new BannerWidget(this, "Analytics Widget", Qt::Vertical, 100);
    bannerWidget->setMaximumHeight(100);

    bannerAndSettingsLayout->addWidget(bannerWidget, 1);

    // config button
    configureButton = new QPushButton(this);
    configureButton->setIcon(loadThemedIcon(QStringLiteral("icons/settings")));
    configureButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    connect(configureButton, &QPushButton::clicked, this, [this] { applyConfigFromDialog(); });
    bannerAndSettingsLayout->addWidget(configureButton, 0);

    layout->addWidget(bannerAndSettingsContainer);

    connect(analyzer, &DeckListStatisticsAnalyzer::statsUpdated, this, &AbstractAnalyticsPanelWidget::updateDisplay);
}

void AbstractAnalyticsPanelWidget::applyConfigFromDialog(std::function<void(bool accepted)> then)
{
    QDialog *dlg = createConfigDialog(this);
    if (!dlg) {
        if (then) {
            then(false);
        }
        return;
    }

    Dialogs::open(dlg, [this, dlg, then = std::move(then)](int result) {
        const bool ok = result == QDialog::Accepted;
        if (ok) {
            // dialog must expose its final config as JSON
            auto newCfg = extractConfigFromDialog(dlg);
            loadConfig(newCfg);
            updateDisplay();
        }
        if (then) {
            then(ok);
        }
    });
}
#ifndef DECK_CARD_ZONE_DISPLAY_WIDGET_H
#define DECK_CARD_ZONE_DISPLAY_WIDGET_H

#include "../../../../deck/deck_list_model.h"
#include "../../../../game/cards/card_info.h"
#include "../general/display/banner_widget.h"
#include "../general/layout_containers/overlap_widget.h"
#include "../visual_deck_editor/visual_deck_editor_widget.h"
#include "card_info_picture_with_text_overlay_widget.h"
#include "card_size_widget.h"

#include <QVBoxLayout>
#include <QWidget>

class DeckCardZoneDisplayWidget : public QWidget
{
    Q_OBJECT

public:
    DeckCardZoneDisplayWidget(QWidget *parent,
                              DeckListModel *deckListModel,
                              QString zoneName,
                              QString activeGroupCriteria,
                              QStringList activeSortCriteria,
                              DisplayType displayType,
                              int bannerOpacity,
                              int subBannerOpacity,
                              CardSizeWidget *_cardSizeWidget);
    DeckListModel *deckListModel;
    QString zoneName;
    void addCardsToOverlapWidget();
    void resizeEvent(QResizeEvent *event) override;

public slots:
    void onClick(QMouseEvent *event, CardInfoPictureWithTextOverlayWidget *card);
    void onHover(CardInfoPtr card);
    void displayCards();
    void refreshDisplayType(const DisplayType &displayType);
    void addCardGroupIfItDoesNotExist();
    void deleteCardGroupIfItDoesNotExist();
    void onActiveGroupCriteriaChanged(QString activeGroupCriteria);
    void onActiveSortCriteriaChanged(QStringList activeSortCriteria);
    QList<QString> getGroupCriteriaValueList();

signals:
    void cardClicked(QMouseEvent *event, CardInfoPictureWithTextOverlayWidget *card, QString zoneName);
    void cardHovered(CardInfoPtr card);
    void activeSortCriteriaChanged(QStringList activeSortCriteria);

private:
    QString activeGroupCriteria;
    QStringList activeSortCriteria;
    DisplayType displayType = DisplayType::Overlap;
    int bannerOpacity = 20;
    int subBannerOpacity = 10;
    CardSizeWidget *cardSizeWidget;
    QVBoxLayout *layout;
    BannerWidget *banner;
    QWidget *cardGroupContainer;
    QVBoxLayout *cardGroupLayout;
    OverlapWidget *overlapWidget;
};

#endif // DECK_CARD_ZONE_DISPLAY_WIDGET_H

/**
 * @file deck_preview_deck_tags_display_widget.h
 * @ingroup VisualDeckPreviewWidgets
 */

#ifndef DECK_PREVIEW_DECK_TAGS_DISPLAY_WIDGET_H
#define DECK_PREVIEW_DECK_TAGS_DISPLAY_WIDGET_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <qtmetamacros.h>

class FlowWidget;

class DeckPreviewDeckTagsDisplayWidget : public QWidget
{
    Q_OBJECT

    QStringList currentTags;
    FlowWidget *flowWidget;
    std::function<QStringList()> knownTagsProvider;
    std::function<void(std::function<void()> thenAllowed)> conversionPromptHandler;

public:
    explicit DeckPreviewDeckTagsDisplayWidget(QWidget *_parent, const QStringList &_tags = {});
    void setTags(const QStringList &_tags);
    void refreshTags();

    /**
     * @brief Sets a provider for the tags shown in the edit dialog.
     * Defaults to scanning all deck files in the deck folder.
     */
    void setKnownTagsProvider(const std::function<QStringList()> &provider);

    /**
     * @brief Sets a handler run before opening the tag dialog. The handler calls
     * thenAllowed to open the dialog; not calling it cancels the dialog. Defaults
     * to no handler (the deck editor path).
     */
    void setConversionPromptHandler(const std::function<void(std::function<void()> thenAllowed)> &handler);

public slots:
    void openTagEditDlg();

signals:
    /**
     * Emitted when the tags have changed due to user interaction.
     * @param tags The new list of tags.
     */
    void tagsChanged(const QStringList &tags);

private:
    void openTagDialog();
};
#endif // DECK_PREVIEW_DECK_TAGS_DISPLAY_WIDGET_H

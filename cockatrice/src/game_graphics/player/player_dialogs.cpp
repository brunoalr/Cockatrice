#include "player_dialogs.h"

#include "../../client/settings/cache_settings.h"
#include "../../client/settings/card_counter_settings.h"
#include "../../interface/widgets/utility/get_text_with_max.h"
#include "../../interface/widgets/utility/non_blocking_dialogs.h"
#include "../board/abstract_counter.h"
#include "../board/card_item.h"
#include "../dialogs/dlg_annotation.h"
#include "../dialogs/dlg_create_token.h"
#include "../dialogs/dlg_move_top_cards_until.h"
#include "../dialogs/dlg_roll_dice.h"
#include "../game_scene.h"
#include "../player/player_graphics_item.h"

#include <QChar>
#include <QInputDialog>
#include <QLineEdit>
#include <QPointer>
#include <libcockatrice/card/relation/card_relation.h>
#include <libcockatrice/game/player/player_actions.h>
#include <libcockatrice/game/player/player_logic.h>
#include <libcockatrice/utility/string_limits.h>

namespace
{
// Server events can destroy a selected card while its dialog is open.
QList<QPointer<CardState>> guarded(const QList<CardState *> &cards)
{
    return {cards.cbegin(), cards.cend()};
}

QList<CardState *> stillAlive(const QList<QPointer<CardState>> &cards)
{
    QList<CardState *> alive;
    for (CardState *card : cards) {
        if (card != nullptr) {
            alive.append(card);
        }
    }
    return alive;
}
} // namespace

PlayerDialogs::PlayerDialogs(PlayerGraphicsItem *_player, PlayerActions *_playerActions)
    : QObject(_player), player(_player), playerActions(_playerActions)
{
    connect(playerActions, &PlayerActions::requestViewTopCardsDialog, this,
            &PlayerDialogs::onViewTopCardsDialogRequested);

    connect(playerActions, &PlayerActions::requestViewBottomCardsDialog, this,
            &PlayerDialogs::onViewBottomCardsDialogRequested);

    connect(playerActions, &PlayerActions::requestShuffleTopDialog, this, &PlayerDialogs::onShuffleTopDialogRequested);

    connect(playerActions, &PlayerActions::requestShuffleBottomDialog, this,
            &PlayerDialogs::onShuffleBottomDialogRequested);

    connect(playerActions, &PlayerActions::requestMulliganDialog, this, &PlayerDialogs::onMulliganDialogRequested);
    connect(playerActions, &PlayerActions::startingHandSizeChosen, this, &PlayerDialogs::onStartingHandSizeChosen);
    connect(playerActions, &PlayerActions::requestDrawCardsDialog, this, &PlayerDialogs::onDrawCardsDialogRequested);

    connect(playerActions, &PlayerActions::requestMoveTopCardsToDialog, this,
            &PlayerDialogs::onMoveTopCardsToDialogRequested);

    connect(playerActions, &PlayerActions::requestMoveTopCardsUntilDialog, this,
            &PlayerDialogs::onMoveTopCardsUntilDialogRequested);

    connect(playerActions, &PlayerActions::requestMoveBottomCardsToDialog, this,
            &PlayerDialogs::onMoveBottomCardsToDialogRequested);

    connect(playerActions, &PlayerActions::requestDrawBottomCardsDialog, this,
            &PlayerDialogs::onDrawBottomCardsDialogRequested);

    connect(playerActions, &PlayerActions::requestRollDieDialog, this, &PlayerDialogs::onRollDieDialogRequested);

    connect(playerActions, &PlayerActions::requestCreateTokenDialog, this,
            &PlayerDialogs::onCreateTokenDialogRequested);

    connect(playerActions, &PlayerActions::requestCreateRelatedFromRelationDialog, this,
            &PlayerDialogs::onCreateRelatedFromRelationDialogRequested);

    connect(playerActions, &PlayerActions::requestMoveCardXCardsFromTopDialog, this,
            &PlayerDialogs::onMoveCardXCardsFromTopDialogRequested);

    connect(playerActions, &PlayerActions::requestSetPTDialog, this, &PlayerDialogs::onSetPTDialogRequested);

    connect(playerActions, &PlayerActions::requestSetAnnotationDialog, this,
            &PlayerDialogs::onSetAnnotationDialogRequested);

    connect(playerActions, &PlayerActions::requestSetCardCounterDialog, this,
            &PlayerDialogs::onSetCardCounterDialogRequested);
}

void PlayerDialogs::onViewTopCardsDialogRequested(int defaultNumberTopCards, int deckSize)
{
    getNumber(tr("View top cards of library"), tr("Number of cards: (max. %1)").arg(deckSize), defaultNumberTopCards, 1,
              deckSize, [this](int number) { playerActions->actViewTopCards(number); });
}

void PlayerDialogs::onViewBottomCardsDialogRequested(int defaultNumberBottomCards, int deckSize)
{
    getNumber(tr("View bottom cards of library"), tr("Number of cards: (max. %1)").arg(deckSize),
              defaultNumberBottomCards, 1, deckSize, [this](int number) { playerActions->actViewBottomCards(number); });
}

void PlayerDialogs::onShuffleTopDialogRequested(int defaultNumberTopCards, int maxCards)
{
    getNumber(tr("Shuffle top cards of library"), tr("Number of cards: (max. %1)").arg(maxCards), defaultNumberTopCards,
              1, maxCards, [this](int number) { playerActions->actShuffleTop(number); });
}

void PlayerDialogs::onShuffleBottomDialogRequested(int defaultNumberBottomCards, int maxCards)
{
    getNumber(tr("Shuffle bottom cards of library"), tr("Number of cards: (max. %1)").arg(maxCards),
              defaultNumberBottomCards, 1, maxCards, [this](int number) { playerActions->actShuffleBottom(number); });
}

void PlayerDialogs::onMulliganDialogRequested(int startSize, int handSize, int deckSize)
{
    getNumber(tr("Draw hand"),
              tr("Number of cards: (max. %1)").arg(deckSize) + '\n' +
                  tr("0 and lower are in comparison to current hand size"),
              startSize, -handSize, deckSize, [this](int number) { playerActions->actMulligan(number); });
}

void PlayerDialogs::onStartingHandSizeChosen(int number)
{
    SettingsCache::instance().userInterface().setStartingHandSize(number);
}

void PlayerDialogs::onDrawCardsDialogRequested(int defaultNumberTopCards, int deckSize)
{
    getNumber(tr("Draw cards"), tr("Number of cards: (max. %1)").arg(deckSize), defaultNumberTopCards, 1, deckSize,
              [this](int number) { playerActions->actDrawCards(number); });
}

void PlayerDialogs::onMoveTopCardsToDialogRequested(int defaultNumberTopCards,
                                                    int maxCards,
                                                    const QString &targetZone,
                                                    const QString &zoneDisplayName,
                                                    bool faceDown)
{
    getNumber(tr("Move top cards to %1").arg(zoneDisplayName), tr("Number of cards: (max. %1)").arg(maxCards),
              defaultNumberTopCards, 1, maxCards, [this, targetZone, faceDown](int number) {
                  playerActions->moveTopCardsTo(number, targetZone, faceDown);
              });
}

void PlayerDialogs::onMoveTopCardsUntilDialogRequested(MoveTopCardsUntilOptions options)
{
    auto *dlg = new DlgMoveTopCardsUntil(dialogParent(), options);
    Dialogs::open(dlg, Dialogs::whileAlive(this, [this, dlg](int result) {
                      if (result == QDialog::Accepted) {
                          playerActions->moveTopCardsUntil(dlg->getExpr(), dlg->getOptions());
                      }
                  }));
}

void PlayerDialogs::onMoveBottomCardsToDialogRequested(int defaultNumberBottomCards,
                                                       int maxCards,
                                                       const QString &targetZone,
                                                       const QString &zoneDisplayName,
                                                       bool faceDown)
{
    getNumber(tr("Move bottom cards to %1").arg(zoneDisplayName), tr("Number of cards: (max. %1)").arg(maxCards),
              defaultNumberBottomCards, 1, maxCards, [this, targetZone, faceDown](int number) {
                  playerActions->moveBottomCardsTo(number, targetZone, faceDown);
              });
}

void PlayerDialogs::onDrawBottomCardsDialogRequested(int defaultNumberBottomCards, int maxCards)
{
    getNumber(tr("Draw bottom cards"), tr("Number of cards: (max. %1)").arg(maxCards), defaultNumberBottomCards, 1,
              maxCards, [this](int number) { playerActions->actDrawBottomCards(number); });
}

void PlayerDialogs::onRollDieDialogRequested()
{
    auto *dlg = new DlgRollDice(dialogParent());
    Dialogs::open(dlg, Dialogs::whileAlive(this, [this, dlg](int result) {
                      if (result == QDialog::Accepted) {
                          playerActions->actRollDie(dlg->getDieSideCount(), dlg->getDiceToRollCount());
                      }
                  }));
}

void PlayerDialogs::onCreateRelatedFromRelationDialogRequested(const CardState *sourceCard,
                                                               const CardRelation *cardRelation,
                                                               const std::function<void()> &thenFinished)
{
    if (sourceCard == nullptr || cardRelation == nullptr) {
        if (thenFinished) {
            thenFinished();
        }
        return;
    }

    if (!cardRelation->getIsVariable()) {
        createRelated(sourceCard, cardRelation, cardRelation->getDefaultCount(), thenFinished);
        return;
    }

    auto *dialog = new QInputDialog(dialogParent());
    dialog->setWindowTitle(tr("Create tokens"));
    dialog->setLabelText(tr("Number:"));
    dialog->setIntRange(1, MAX_TOKENS_PER_DIALOG);
    dialog->setIntValue(cardRelation->getDefaultCount());
    openHoldingDialogSemaphore(dialog,
                               [this, dialog, source = QPointer<const CardState>(sourceCard),
                                relation = QPointer<const CardRelation>(cardRelation), thenFinished](int result) {
                                   if (!source || !relation) {
                                       return;
                                   }
                                   if (result == QDialog::Accepted) {
                                       createRelated(source, relation, dialog->intValue(), thenFinished);
                                   } else if (thenFinished) {
                                       thenFinished();
                                   }
                               });
}

void PlayerDialogs::createRelated(const CardState *sourceCard,
                                  const CardRelation *cardRelation,
                                  int count,
                                  const std::function<void()> &thenFinished)
{
    if (playerActions->createRelatedFromRelation(sourceCard, cardRelation, count)) {
        playerActions->onRelatedCardCreated(sourceCard, cardRelation); // only on confirmed success
    }
    if (thenFinished) {
        thenFinished();
    }
}

void PlayerDialogs::onCreateTokenDialogRequested(const QStringList &predefinedTokens)
{
    auto *dlg = new DlgCreateToken(predefinedTokens, dialogParent());
    Dialogs::open(dlg, Dialogs::whileAlive(this, [this, dlg](int result) {
                      if (result == QDialog::Accepted) {
                          playerActions->actCreateToken(dlg->getTokenInfo());
                      }
                  }));
}

void PlayerDialogs::onMoveCardXCardsFromTopDialogRequested(int defaultNumberTopCardsToPlaceBelow, int deckSize)
{
    getNumber(tr("Place card X cards from top of library"),
              tr("Which position should this card be placed:") + "\n" + tr("(max. %1)").arg(deckSize),
              defaultNumberTopCardsToPlaceBelow, 1, deckSize, [this](int number) {
                  number -= 1; // indexes start at 0
                  playerActions->actMoveCardXCardsFromTop(player->getGameScene()->selectedCardStates(), number);
              });
}

void PlayerDialogs::onSetPTDialogRequested(const QString &oldPT)
{
    const auto cards = guarded(player->getGameScene()->selectedCardStates());
    QInputDialog *dialog = newTextDialogWithMax(dialogParent(), tr("Change power/toughness"), tr("Change stats to:"),
                                                QLineEdit::Normal, oldPT);
    openHoldingDialogSemaphore(dialog, [this, dialog, cards](int result) {
        if (result != QDialog::Accepted || player->getLogic()->clearCardsToDelete()) {
            return;
        }

        playerActions->actSetPT(stillAlive(cards), dialog->textValue());
    });
}

void PlayerDialogs::onSetAnnotationDialogRequested(const QString &oldAnnotation)
{
    const auto cards = guarded(player->getGameScene()->selectedCardStates());
    auto *dialog = new AnnotationDialog(dialogParent());
    dialog->setOptions(QInputDialog::UsePlainTextEditForTextInput);
    dialog->setWindowTitle(tr("Set annotation"));
    dialog->setLabelText(tr("Please enter the new annotation:"));
    dialog->setTextValue(oldAnnotation);
    openHoldingDialogSemaphore(dialog, [this, dialog, cards](int result) {
        if (result != QDialog::Accepted || player->getLogic()->clearCardsToDelete()) {
            return;
        }
        QString annotation = dialog->textValue().left(MAX_NAME_LENGTH);
        playerActions->actSetAnnotation(stillAlive(cards), annotation);
    });
}

void PlayerDialogs::onSetCardCounterDialogRequested(int counterId, const QString &oldValueForDlg)
{
    const auto cards = guarded(player->getGameScene()->selectedCardStates());

    auto &cardCounterSettings = SettingsCache::instance().cardCounters();
    QString counterName = cardCounterSettings.displayName(counterId);

    auto *dialog = new AbstractCounterDialog(counterName, oldValueForDlg, dialogParent());
    openHoldingDialogSemaphore(dialog, [this, dialog, cards, counterId](int result) {
        if (result != QDialog::Accepted || player->getLogic()->clearCardsToDelete()) {
            return;
        }
        playerActions->actSetCardCounter(stillAlive(cards), counterId, dialog->textValue());
    });
}

void PlayerDialogs::getNumber(const QString &title,
                              const QString &label,
                              int value,
                              int minimum,
                              int maximum,
                              const std::function<void(int number)> &thenAccepted)
{
    Dialogs::getInt(dialogParent(), title, label, value, minimum, maximum, Dialogs::whileAlive(this, thenAccepted));
}

void PlayerDialogs::openHoldingDialogSemaphore(QDialog *dialog, const std::function<void(int result)> &thenFinished)
{
    emit requestDialogSemaphore(true);
    // A dialog destroyed with its parent never reports back.
    const auto releaseUnanswered =
        connect(dialog, &QObject::destroyed, this, [this] { emit requestDialogSemaphore(false); });
    Dialogs::open(dialog, Dialogs::whileAlive(this, [this, releaseUnanswered, thenFinished](int result) {
                      disconnect(releaseUnanswered);
                      emit requestDialogSemaphore(false);
                      thenFinished(result);
                  }));
}

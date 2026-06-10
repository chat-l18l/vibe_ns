#ifndef FSM_H
#define FSM_H

/**
 * @file fsm.h
 * @brief Generic table-driven finite state machine engine.
 *
 * A state machine is described declaratively as an array of
 * ::fsm_transition_t rows (state, event) → (action, next_state,
 * error_state). The engine itself holds no domain knowledge; per-machine
 * context travels through the @c ctx pointer into the action callbacks.
 *
 * Design notes:
 *  - Lookup is a linear scan over the table — O(n), trivially cache-friendly,
 *    and fine for the table sizes used here (≤ ~20 rows).
 *  - Unmatched (state, event) pairs are ignored (logged at debug level),
 *    so the table only needs rows for transitions that do something.
 *  - The engine is not re-entrant: an action must not call fsm_dispatch()
 *    on its own machine, because the engine writes @c current_state after
 *    the action returns and would overwrite the nested transition. Raise a
 *    flag and dispatch after the outer call returns instead (see
 *    telnet_session.c, @c game_over_pending).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint16_t fsm_state_t;  /**< State identifier (machine-specific enum). */
typedef uint16_t fsm_event_t;  /**< Event identifier (machine-specific enum). */

/** Result of a transition action; selects next_state vs error_state. */
typedef enum {
    FSM_ACTION_OK    = 0,  /**< Take the transition's @c next_state. */
    FSM_ACTION_ERROR = 1   /**< Take the transition's @c error_state. */
} fsm_action_result_t;

/** One row of a transition table. */
typedef struct {
    fsm_state_t          state;        /**< Source state to match. */
    fsm_event_t          event;        /**< Event to match. */
    /** Optional side effect; NULL means "transition without action". */
    fsm_action_result_t  (*action)(void *ctx, fsm_event_t ev, const void *ev_data);
    fsm_state_t          next_state;   /**< Target on ::FSM_ACTION_OK. */
    fsm_state_t          error_state;  /**< Target on ::FSM_ACTION_ERROR. */
} fsm_transition_t;

/** Immutable machine definition; shared by all instances of one machine type. */
typedef struct {
    const fsm_transition_t *table;          /**< Transition rows. */
    size_t                  table_len;      /**< Number of rows. */
    fsm_state_t             initial_state;  /**< State after fsm_init(). */
    fsm_state_t             terminal_state; /**< State in which dispatch stops. */
} fsm_def_t;

/** One live machine instance: definition pointer + current state. */
typedef struct {
    const fsm_def_t *def;
    fsm_state_t      current_state;
} fsm_t;

/**
 * @brief Initialize an instance to the definition's initial state.
 * @param fsm  Instance to initialize (caller-owned storage).
 * @param def  Machine definition; must outlive the instance.
 */
void fsm_init        (fsm_t *fsm, const fsm_def_t *def);

/**
 * @brief Feed one event into the machine.
 *
 * Finds the first table row matching (current_state, @p ev), runs its
 * action (if any) and moves to next_state or error_state depending on
 * the action result.
 *
 * @param fsm      Machine instance.
 * @param ev       Event to dispatch.
 * @param ctx      Opaque context forwarded to the action callback.
 * @param ev_data  Optional event payload forwarded to the action callback.
 * @return 0 if a transition matched, -1 if the event was ignored.
 */
int  fsm_dispatch    (fsm_t *fsm, fsm_event_t ev, void *ctx, const void *ev_data);

/**
 * @brief Check whether the machine has reached its terminal state.
 */
bool fsm_is_terminal (const fsm_t *fsm);

#endif /* FSM_H */

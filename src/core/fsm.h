#ifndef FSM_H
#define FSM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef uint16_t fsm_state_t;
typedef uint16_t fsm_event_t;

typedef enum {
    FSM_ACTION_OK    = 0,
    FSM_ACTION_ERROR = 1
} fsm_action_result_t;

typedef struct {
    fsm_state_t          state;
    fsm_event_t          event;
    fsm_action_result_t  (*action)(void *ctx, fsm_event_t ev, const void *ev_data);
    fsm_state_t          next_state;
    fsm_state_t          error_state;
} fsm_transition_t;

typedef struct {
    const fsm_transition_t *table;
    size_t                  table_len;
    fsm_state_t             initial_state;
    fsm_state_t             terminal_state;
} fsm_def_t;

typedef struct {
    const fsm_def_t *def;
    fsm_state_t      current_state;
} fsm_t;

void fsm_init        (fsm_t *fsm, const fsm_def_t *def);
int  fsm_dispatch    (fsm_t *fsm, fsm_event_t ev, void *ctx, const void *ev_data);
bool fsm_is_terminal (const fsm_t *fsm);

#endif /* FSM_H */

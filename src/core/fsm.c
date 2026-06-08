#include "fsm.h"
#include "log.h"

#include <assert.h>

void
fsm_init (fsm_t *fsm, const fsm_def_t *def)
{
    assert (fsm != NULL);
    assert (def != NULL);
    assert (def->table != NULL);
    assert (def->table_len > 0);

    fsm->def           = def;
    fsm->current_state = def->initial_state;
}

int
fsm_dispatch (fsm_t *fsm, fsm_event_t ev, void *ctx, const void *ev_data)
{
    assert (fsm != NULL);
    assert (fsm->def != NULL);

    const fsm_def_t *def = fsm->def;
    for (size_t i = 0; i < def->table_len; i++) {
        const fsm_transition_t *t = &def->table[i];
        if (t->state != fsm->current_state || t->event != ev)
            continue;

        fsm_action_result_t result = FSM_ACTION_OK;
        if (t->action != NULL)
            result = t->action (ctx, ev, ev_data);

        fsm->current_state = (result == FSM_ACTION_OK)
                           ? t->next_state
                           : t->error_state;
        return 0;
    }

    LOG_DBG ("fsm: no transition for state=%u event=%u — ignored",
             fsm->current_state, ev);
    return -1;
}

bool
fsm_is_terminal (const fsm_t *fsm)
{
    assert (fsm != NULL);
    assert (fsm->def != NULL);
    return fsm->current_state == fsm->def->terminal_state;
}

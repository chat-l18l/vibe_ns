#include "protocol.h"
#include "telnet/telnet_session.h"

const protocol_ops_t *protocol_registry[] = {
    &telnet_protocol,
    NULL
};

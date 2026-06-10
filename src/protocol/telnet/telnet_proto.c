/**
 * @file telnet_proto.c
 * @brief IAC parser state machine and outgoing sequence builders.
 */

#include "telnet_proto.h"
#include "../../core/log.h"

#include <string.h>
#include <assert.h>

void
iac_parser_init (iac_parser_t *parser)
{
    assert (parser != NULL);
    memset (parser, 0, sizeof (*parser));
    parser->state = IAC_DATA;
}

void
iac_parser_feed (iac_parser_t *parser, const uint8_t *raw, size_t len,
                 void *ctx, const iac_callbacks_t *cbs)
{
    assert (parser != NULL);
    assert (cbs    != NULL);

    if (!raw || len == 0)
        return;

    const uint8_t *data_start = NULL;
    size_t         data_len   = 0;

#define FLUSH_DATA() \
    do { \
        if (data_len > 0 && cbs->on_data) \
            cbs->on_data (ctx, data_start, data_len); \
        data_start = NULL; data_len = 0; \
    } while (0)

    for (size_t i = 0; i < len; i++) {
        uint8_t b = raw[i];

        switch (parser->state) {
        case IAC_DATA:
            if (b == TELNET_IAC) {
                FLUSH_DATA ();
                parser->state = IAC_IAC;
            } else {
                if (!data_start) data_start = &raw[i];
                data_len++;
            }
            break;

        case IAC_IAC:
            if (b == TELNET_IAC) {
                uint8_t lit = 0xFF;
                if (cbs->on_data) cbs->on_data (ctx, &lit, 1);
                parser->state = IAC_DATA;
            } else if (b == TELNET_SB) {
                parser->sb_len = 0;
                parser->state  = IAC_SB;
            } else if (b == TELNET_WILL || b == TELNET_WONT ||
                       b == TELNET_DO   || b == TELNET_DONT) {
                parser->cmd   = b;
                parser->state = IAC_CMD;
            } else {
                parser->state = IAC_DATA;
            }
            break;

        case IAC_CMD: {
            iac_option_event_t ev = { .opt = b, .cmd = parser->cmd };
            if (cbs->on_option) cbs->on_option (ctx, &ev);
            parser->state = IAC_DATA;
            break;
        }

        case IAC_SB:
            if (b == TELNET_IAC) {
                parser->state = IAC_SB_IAC;
            } else if (parser->sb_len == 0) {
                parser->sb_opt = b;
                parser->sb_len++;
            } else if (parser->sb_len < (uint8_t) sizeof (parser->sb_buf)) {
                parser->sb_buf[parser->sb_len - 1] = b;
                parser->sb_len++;
            }
            /* else: overflow — silently drop, DoS protection */
            break;

        case IAC_SB_IAC:
            if (b == TELNET_SE) {
                iac_sb_event_t ev;
                ev.opt = parser->sb_opt;
                ev.len = (parser->sb_len > 1) ? (uint8_t)(parser->sb_len - 1) : 0;
                memcpy (ev.data, parser->sb_buf, ev.len);
                if (cbs->on_subneg) cbs->on_subneg (ctx, &ev);
                parser->state = IAC_DATA;
            } else {
                parser->state = IAC_SB;
            }
            break;

        default:
            assert (0 && "unreachable IAC parser state");
        }
    }

    FLUSH_DATA ();
#undef FLUSH_DATA
}

int iac_write_will (uint8_t *buf, size_t sz, uint8_t opt) {
    if (!buf || sz < 3) return -1;
    buf[0] = TELNET_IAC; buf[1] = TELNET_WILL; buf[2] = opt; return 3;
}
int iac_write_wont (uint8_t *buf, size_t sz, uint8_t opt) {
    if (!buf || sz < 3) return -1;
    buf[0] = TELNET_IAC; buf[1] = TELNET_WONT; buf[2] = opt; return 3;
}
int iac_write_do (uint8_t *buf, size_t sz, uint8_t opt) {
    if (!buf || sz < 3) return -1;
    buf[0] = TELNET_IAC; buf[1] = TELNET_DO; buf[2] = opt; return 3;
}
int iac_write_dont (uint8_t *buf, size_t sz, uint8_t opt) {
    if (!buf || sz < 3) return -1;
    buf[0] = TELNET_IAC; buf[1] = TELNET_DONT; buf[2] = opt; return 3;
}

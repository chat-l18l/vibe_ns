#ifndef TELNET_PROTO_H
#define TELNET_PROTO_H

#include <stdint.h>
#include <stddef.h>

/* Telnet command bytes */
#define TELNET_IAC   255
#define TELNET_DONT  254
#define TELNET_DO    253
#define TELNET_WONT  252
#define TELNET_WILL  251
#define TELNET_SB    250
#define TELNET_SE    240
#define TELNET_GA    249
#define TELNET_NOP   241

/* Telnet options */
#define TELOPT_ECHO   1
#define TELOPT_SGA    3
#define TELOPT_TTYPE  24
#define TELOPT_NAWS   31

/* IAC parser states */
typedef enum {
    IAC_DATA   = 0,
    IAC_IAC    = 1,
    IAC_CMD    = 2,
    IAC_SB     = 3,
    IAC_SB_IAC = 4,
} iac_parse_state_t;

typedef struct {
    uint8_t  opt;
    uint8_t  cmd;   /* WILL/WONT/DO/DONT */
} iac_option_event_t;

typedef struct {
    uint8_t  opt;
    uint8_t  data[255];
    uint8_t  len;
} iac_sb_event_t;

typedef struct {
    void (*on_data)(void *ctx, const uint8_t *buf, size_t len);
    void (*on_option)(void *ctx, const iac_option_event_t *ev);
    void (*on_subneg)(void *ctx, const iac_sb_event_t *ev);
} iac_callbacks_t;

typedef struct {
    iac_parse_state_t  state;
    uint8_t            cmd;
    uint8_t            sb_buf[255];
    uint8_t            sb_len;
    uint8_t            sb_opt;
} iac_parser_t;

void iac_parser_init(iac_parser_t *parser);
void iac_parser_feed(iac_parser_t *parser, const uint8_t *raw, size_t len,
                     void *ctx, const iac_callbacks_t *cbs);

/* Build outgoing IAC sequences into buf. Return bytes written. */
int iac_write_will(uint8_t *buf, size_t sz, uint8_t opt);
int iac_write_wont(uint8_t *buf, size_t sz, uint8_t opt);
int iac_write_do  (uint8_t *buf, size_t sz, uint8_t opt);
int iac_write_dont(uint8_t *buf, size_t sz, uint8_t opt);

#endif /* TELNET_PROTO_H */

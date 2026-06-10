#ifndef TELNET_PROTO_H
#define TELNET_PROTO_H

/**
 * @file telnet_proto.h
 * @brief Telnet IAC byte-stream parser and option negotiation (RFC 854/855).
 *
 * The parser is a push-style state machine: feed it raw socket bytes and
 * it emits three kinds of events through ::iac_callbacks_t —
 * plain data, option negotiation (WILL/WONT/DO/DONT) and subnegotiation
 * blocks (IAC SB … IAC SE, e.g. NAWS window size, RFC 1073).
 *
 * The parser holds no session knowledge; it is pure protocol decoding
 * and is independently unit-testable.
 */

#include <stdint.h>
#include <stddef.h>

/** @name Telnet command bytes (RFC 854) */
/**@{*/
#define TELNET_IAC   255  /**< Interpret As Command escape. */
#define TELNET_DONT  254
#define TELNET_DO    253
#define TELNET_WONT  252
#define TELNET_WILL  251
#define TELNET_SB    250  /**< Subnegotiation begin. */
#define TELNET_SE    240  /**< Subnegotiation end. */
#define TELNET_GA    249  /**< Go ahead. */
#define TELNET_NOP   241
/**@}*/

/** @name Telnet option codes */
/**@{*/
#define TELOPT_ECHO   1   /**< RFC 857 — server echo. */
#define TELOPT_SGA    3   /**< RFC 858 — suppress go-ahead (char mode). */
#define TELOPT_TTYPE  24  /**< RFC 1091 — terminal type. */
#define TELOPT_NAWS   31  /**< RFC 1073 — negotiate about window size. */
/**@}*/

/** Parser states. */
typedef enum {
    IAC_DATA   = 0,  /**< Plain data stream. */
    IAC_IAC    = 1,  /**< Seen IAC; expecting command byte. */
    IAC_CMD    = 2,  /**< Seen WILL/WONT/DO/DONT; expecting option byte. */
    IAC_SB     = 3,  /**< Inside subnegotiation payload. */
    IAC_SB_IAC = 4,  /**< Seen IAC inside SB; expecting SE. */
} iac_parse_state_t;

/** Option negotiation event (IAC WILL/WONT/DO/DONT <opt>). */
typedef struct {
    uint8_t  opt;   /**< Option code (TELOPT_*). */
    uint8_t  cmd;   /**< TELNET_WILL / WONT / DO / DONT. */
} iac_option_event_t;

/** Subnegotiation event (IAC SB <opt> <data…> IAC SE). */
typedef struct {
    uint8_t  opt;        /**< Option code. */
    uint8_t  data[255];  /**< Payload (excess silently dropped). */
    uint8_t  len;        /**< Payload length. */
} iac_sb_event_t;

/** Event sinks for iac_parser_feed(). Any callback may be NULL. */
typedef struct {
    void (*on_data)(void *ctx, const uint8_t *buf, size_t len);     /**< Plain bytes. */
    void (*on_option)(void *ctx, const iac_option_event_t *ev);     /**< Negotiation. */
    void (*on_subneg)(void *ctx, const iac_sb_event_t *ev);         /**< SB block. */
} iac_callbacks_t;

/** Parser instance; persists across feed() calls (commands may span reads). */
typedef struct {
    iac_parse_state_t  state;
    uint8_t            cmd;          /**< Pending WILL/WONT/DO/DONT. */
    uint8_t            sb_buf[255];  /**< Subnegotiation accumulator. */
    uint8_t            sb_len;
    uint8_t            sb_opt;
} iac_parser_t;

/** @brief Reset a parser to the initial (data) state. */
void iac_parser_init(iac_parser_t *parser);

/**
 * @brief Push raw socket bytes through the parser.
 *
 * Decodes telnet commands and invokes the callbacks in input order.
 * Plain data is delivered in maximal contiguous slices of @p raw
 * (zero-copy); escaped IAC-IAC bytes are delivered separately.
 *
 * @param parser  Parser state.
 * @param raw     Received bytes (may be NULL when len is 0).
 * @param len     Byte count.
 * @param ctx     Opaque pointer forwarded to all callbacks.
 * @param cbs     Event sinks; must not be NULL.
 */
void iac_parser_feed(iac_parser_t *parser, const uint8_t *raw, size_t len,
                     void *ctx, const iac_callbacks_t *cbs);

/** @name Outgoing IAC sequence builders
 *  Write a 3-byte IAC sequence into @p buf.
 *  @return Bytes written (3), or -1 if @p buf is too small.
 */
/**@{*/
int iac_write_will(uint8_t *buf, size_t sz, uint8_t opt);
int iac_write_wont(uint8_t *buf, size_t sz, uint8_t opt);
int iac_write_do  (uint8_t *buf, size_t sz, uint8_t opt);
int iac_write_dont(uint8_t *buf, size_t sz, uint8_t opt);
/**@}*/

#endif /* TELNET_PROTO_H */

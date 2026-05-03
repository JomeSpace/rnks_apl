/* Shared data types for Go-Back-N ARQ protocol */

#ifndef DATA_H_INCLUDED
#define DATA_H_INCLUDED

/* Error strings defined in error.c */
extern char *errorTable[];

/* Buffer size for data payloads */
#ifndef BufferSize
#define BufferSize 512
#endif

/* Application-level data unit (payload without sequence numbers) */
struct app_unit {
    unsigned long len;              /* Payload length in bytes               */
    char          data[BufferSize]; /* Payload data                          */
};

/* Request packet (client -> server)
 *
 * ReqType:
 *   ReqHello : Connection setup
 *   ReqData  : Data packet
 *   ReqClose : Transfer complete
 *
 * SeNr   : Packet number (0, 1, 2, ...) in the ARQ protocol
 * FlNr   : Payload length in bytes
 */
struct request {
    unsigned char  ReqType;
#define ReqHello 'H'
#define ReqData  'D'
#define ReqClose 'C'

    unsigned long  FlNr;   /* Payload length in bytes                       */
    unsigned long  SeNr;   /* Sequence number (packet number, not byte pos) */

    char           name[BufferSize];  /* Payload data                       */
};

/* Error codes for AnswWarn / AnswErr.
 * In AnswOk, SeNo has a different meaning (see struct answer).
 */
enum {
    ERR_NONE            = 0,
    ERR_WRONG_SEQ       = 1, /* Wrong sequence number / out-of-order        */
    ERR_FILE_ERROR      = 2, /* File I/O error                              */
    ERR_ILLEGAL_REQUEST = 3, /* Invalid ReqType / protocol violation         */
    /* 4-6 reserved for additional ARQ error codes */
    ERR_INTERNAL        = 7
};

/* Server response packet
 *
 * SeNo meaning depends on AnswType:
 *  - AnswOk  : next expected packet number (cumulative ACK)
 *  - AnswWarn/AnswErr : error code (ERR_*)
 */
struct answer {
    unsigned char AnswType;
#define AnswHello 'H'
#define AnswOk    'O'
#define AnswWarn  'W'
#define AnswErr   0xFF

    unsigned long FlNr;  /* Currently unused, reserved                      */
    unsigned long SeNo;  /* See above                                       */

#define ErrNo SeNo       /* Alias: for Warn/Err, SeNo holds the error code  */
};

/* ARQ protocol parameters */
#define GBN_MAX_WINDOW       10
#define GBN_BUFFER_SIZE      (2 * GBN_MAX_WINDOW)
#define GBN_TIMEOUT_INT_MS   100  /* Interval duration in milliseconds      */
#define GBN_TIMEOUT_UNITS    3    /* Timeout in units of TIMEOUT_INT        */

#endif /* DATA_H_INCLUDED */

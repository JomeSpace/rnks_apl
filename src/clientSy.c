#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/select.h>

#include "data.h"
#include "config.h"
#include "clientSy.h"


static int gl_sockfd;
static unsigned long seq_num = 0;
static unsigned long base = 0;
static unsigned int timeout_counter = 0;

struct buf {
    char data[BufferSize];
    unsigned long len;
};
struct buf buffer[GBN_BUFFER_SIZE];

/* --------------------------------------------------------------- */
/*  Initialization / Teardown                                      */
/* --------------------------------------------------------------- */

void initClient(char *name, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *res, *rp;
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;

    ret = getaddrinfo(name, port, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {

        gl_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (gl_sockfd == -1)
            continue;

        int flags = fcntl(gl_sockfd, F_GETFL, 0);
        fcntl(gl_sockfd, F_SETFL, flags | O_NONBLOCK);

        /* connect() on UDP sets the default destination for send()/recv() */
        if (connect(gl_sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(gl_sockfd);
        gl_sockfd = -1;
    }

    freeaddrinfo(res);
    if (gl_sockfd == -1) {
        fprintf(stderr, "Could not connect to server\n");
        exit(EXIT_FAILURE);
    }
}

void closeClient(void)
{
    if (gl_sockfd != -1 && gl_sockfd != 0) {
        close(gl_sockfd);
    }
    gl_sockfd = -1;
    printf("Client closed successfully\n");
}

/* --------------------------------------------------------------- */
/*  Internal helpers                                               */
/* --------------------------------------------------------------- */

static void warteIntervallRest(struct timeval *start, long interval_us)
{
    struct timeval end;
    gettimeofday(&end, NULL);
    long elapsed = (end.tv_sec - start->tv_sec) * 1000000L + (end.tv_usec - start->tv_usec);
    long remaining = interval_us - elapsed;

    if (remaining > 0) {
        struct timeval rest = { remaining / 1000000L, remaining % 1000000L };
        select(0, NULL, NULL, NULL, &rest);
    }
}

/* --------------------------------------------------------------- */
/*  GBN/ARQ send logic                                             */
/* --------------------------------------------------------------- */

static struct answer* doRequest(struct request* req, int winSize, int* windowFull, int* retransmission)
{
    static struct answer ans;
    static unsigned long retransmit_seq = 0;
    static int retransmit_active = 0;

    long interval_us = GBN_TIMEOUT_INT_MS * 1000L;
    struct answer *ret = NULL;
    unsigned long prev_base = base;

    /* =========================================================
     * HELLO / CLOSE
     * ========================================================= */
    if (req->ReqType == ReqHello || req->ReqType == ReqClose) {
        unsigned int hc_timeout_counter = 0;
        int sent = 0;

        retransmit_active = 0;
        retransmit_seq = 0;

        while (1) {
            struct timeval start;
            gettimeofday(&start, NULL);

            if (!sent || hc_timeout_counter >= GBN_TIMEOUT_UNITS) {
                send(gl_sockfd, req, sizeof(*req), 0);
                sent = 1;
                hc_timeout_counter = 0;
            }

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(gl_sockfd, &rfds);
            struct timeval timeout = { 0, interval_us };

            if (select(gl_sockfd + 1, &rfds, NULL, NULL, &timeout) > 0) {
                if (recv(gl_sockfd, &ans, sizeof(ans), 0) == sizeof(ans)) {
                    int valid = (ans.AnswType == AnswOk || ans.AnswType == AnswErr);
                    if (req->ReqType == ReqHello && ans.AnswType == AnswHello)
                        valid = 1;

                    if (valid) {
                        warteIntervallRest(&start, interval_us);
                        return &ans;
                    }
                }
            }

            warteIntervallRest(&start, interval_us);
            hc_timeout_counter++;
        }
    }

    /* =========================================================
     * DATA — Go-Back-N algorithm
     * ========================================================= */
    struct timeval start;
    gettimeofday(&start, NULL);

    if (base == seq_num)
        retransmit_active = 0;

    if (base < seq_num && timeout_counter >= GBN_TIMEOUT_UNITS) {
        printf("[TIMEOUT] Starting retransmit from SeqNr=%lu (to %lu)\n", base, seq_num - 1);
        retransmit_active = 1;
        retransmit_seq = base;
        timeout_counter = 0;
    }

    /* ----- SEND: retransmit takes priority over new packets ----- */
    if (retransmit_active && retransmit_seq < seq_num) {
        unsigned int idx = retransmit_seq % GBN_BUFFER_SIZE;
        req->ReqType = ReqData;
        req->SeNr = retransmit_seq;
        req->FlNr = buffer[idx].len;
        memcpy(req->name, buffer[idx].data, buffer[idx].len);

        send(gl_sockfd, req, sizeof(*req), 0);
        printf("[RETX] SeqNr=%lu\n", retransmit_seq);

        if (retransmission) *retransmission = 1;
        retransmit_seq++;

        if (retransmit_seq >= seq_num)
            retransmit_active = 0;

    } else if (req->ReqType == ReqData && req->FlNr > 0) {
        if (seq_num >= base + (unsigned long)winSize ||
            (seq_num - base) >= GBN_BUFFER_SIZE) {
            printf("Window FULL\n");
            if (windowFull) *windowFull = 1;
        } else {
            unsigned int idx = seq_num % GBN_BUFFER_SIZE;
            buffer[idx].len = req->FlNr;
            memcpy(buffer[idx].data, req->name, req->FlNr);
            req->SeNr = seq_num;
            send(gl_sockfd, req, sizeof(*req), 0);
            printf("[SEND] SeqNr=%lu\n", seq_num);
            seq_num++;
        }
    }

    /* ----- RECEIVE: wait for cumulative ACK ----- */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(gl_sockfd, &rfds);
    struct timeval timeout = { 0, interval_us };

    if (select(gl_sockfd + 1, &rfds, NULL, NULL, &timeout) > 0) {
        if (recv(gl_sockfd, &ans, sizeof(ans), 0) == sizeof(ans)) {
            if (ans.AnswType == AnswOk) {
                unsigned long ack_no = ans.SeNo;

                if (ack_no < base || ack_no > seq_num) {
                    printf("[DROP] ACK outside window: %lu\n", ack_no);
                } else {
                    ret = &ans;
                    if (ack_no > base) {
                        base = ack_no;
                        timeout_counter = 0;

                        if (retransmit_active && retransmit_seq < base)
                            retransmit_seq = base;
                        if (base == seq_num)
                            retransmit_active = 0;
                    }
                    printf("[RECV] ACK next=%lu (base=%lu)\n", ack_no, base);
                }
            } else if (ans.AnswType == AnswErr) {
                ret = &ans;
            }
        }
    }

    warteIntervallRest(&start, interval_us);

    /* ----- TIMER: update timeout counter ----- */
    if (base < seq_num) {
        timeout_counter = (base == prev_base) ? timeout_counter + 1 : 0;
    } else {
        timeout_counter = 0;
    }

    return ret;
}

int arqSendHello(int winSize)
{
    memset(buffer, 0, sizeof(buffer));
    seq_num = 0;
    base = 0;
    timeout_counter = 0;

    struct request hello;
    memset(&hello, 0, sizeof(struct request));

    hello.ReqType = ReqHello;
    hello.SeNr = 0;
    hello.FlNr = 0;
    int window_full = 0;
    int retransmission = 0;

    struct answer *response = doRequest(&hello, winSize, &window_full, &retransmission);
    if (!response) {
        printf("Hello timeout - no response\n");
        return -1;
    }
    switch (response->AnswType) {
        case AnswHello:
            printf("Server Hello ACK - connection ready\n");
            return 0;
        case AnswOk:
            printf("Server AnswOk   - connection ready\n");
            return 0;
        case AnswErr:
            printf("Server Error (ErrNo=%lu)\n", response->ErrNo);
            return -1;
    }
    printf("arqSendHello failed\n");
    return -1;
}

int arqSendData(const struct app_unit *app, int winSize)
{
    struct request data;
    memset(&data, 0, sizeof(data));

    data.ReqType = ReqData;
    data.SeNr = seq_num;
    data.FlNr = app->len > BufferSize ? BufferSize : app->len;

    memcpy(data.name, app->data, data.FlNr);

    int window_full = 0;
    int retransmission = 0;

    struct answer* response = doRequest(&data, winSize, &window_full, &retransmission);

    if (window_full || retransmission) {
        return 1;
    }
    if (response && response->AnswType == AnswErr) {
        printf("Server Error (ErrNo=%lu)\n", response->ErrNo);
        return -1;
    }

    return 0;
}

int arqSendClose(int winSize)
{
    struct request close_req;
    memset(&close_req, 0, sizeof(close_req));
    close_req.ReqType = ReqClose;
    close_req.SeNr = seq_num;
    close_req.FlNr = 0;

    /* Drain: wait until all in-flight packets are acknowledged */
    while (base < seq_num) {
        struct request tick;
        memset(&tick, 0, sizeof(tick));
        tick.ReqType = ReqData;
        tick.SeNr = 0;
        tick.FlNr = 0;
        int window_full = 0;
        int retransmission = 0;
        struct answer* resp = doRequest(&tick, winSize, &window_full, &retransmission);
        if (resp && resp->AnswType == AnswErr) {
            fprintf(stderr, "Client: error draining buffer (ErrNo=%lu).\n", resp->ErrNo);
            return -1;
        }
    }

    int window_full = 0;
    int retransmission = 0;
    struct answer* response = doRequest(&close_req, winSize, &window_full, &retransmission);
    if (!response) {
        fprintf(stderr, "Client: close timeout, exiting.\n");
        return -1;
    }
    if (response->AnswType == AnswOk) {
        printf("Server AnswOk - connection closed\n");
        return 0;
    }
    if (response->AnswType == AnswErr) {
        printf("Server Error (ErrNo=%lu)\n", response->ErrNo);
        return -1;
    }
    return -1;
}

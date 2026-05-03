/* serverSy.c — UDP + ARQ/Go-Back-N server with loss simulation
 *
 * Layers:
 *   - SAP layer (UDP): initServer, getRequest, sendAnswer, exitServer
 *   - ARQ layer: processRequest(), arqServerLoop()
 *
 * Application logic (file open/write/close) is injected via callbacks
 * from server.c: appStartFn, appWriteFn, appEndFn
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>


#include "data.h"
#include "config.h"
#include "serverSy.h"

static int helloDone = 0;
static int closeDone = 0;
static int server_running = 1;
static int gl_sockfd;
struct sockaddr_storage client_addr;
static socklen_t client_addr_len;
static unsigned long expected_seq = 0;
static appStartFn gl_appStartFn;
static appWriteFn gl_appWriteFn;
static appEndFn gl_appEndFn;

/* --------------------------------------------------------------- */
/*  SAP layer (UDP)                                                */
/* --------------------------------------------------------------- */

/* Initialize UDP/IPv6 server socket and bind to port */
int initServer(const char *port)
{
    struct addrinfo hints;
    struct addrinfo* res, * tmp;
    int sockfd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = DEFAULT_FAMILY;
    hints.ai_socktype = DEFAULT_SOCKTYPE;
    hints.ai_flags = AI_PASSIVE;

    int ret = getaddrinfo(NULL, port, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    for (tmp = res; tmp != NULL; tmp = tmp->ai_next) {
        sockfd = socket(tmp->ai_family, tmp->ai_socktype, tmp->ai_protocol);
        if (sockfd >= 0) {
            if (bind(sockfd, tmp->ai_addr, tmp->ai_addrlen) == 0) {
                gl_sockfd = sockfd;
                freeaddrinfo(res);
                printf("completed: initServer() from arqServerLoop()\n");
                return 0;
            }
            close(sockfd);
        }
    }

    fprintf(stderr, "Server: failed to bind socket\n");
    freeaddrinfo(res);
    if (sockfd >= 0) {
        close(sockfd);
    }
    return -1;
}

/* Receive a request packet from the client */
struct request *getRequest(void)
{
    struct request *req = malloc(sizeof(struct request));
    if (!req) return NULL;

    socklen_t addr_len = sizeof(client_addr);
    ssize_t bytes_received;

    bytes_received = recvfrom(gl_sockfd, req, sizeof(struct request), 0, (struct sockaddr*)&client_addr, &addr_len);
    client_addr_len = addr_len;

    if (bytes_received == -1) {
        printf("recvfrom error: %s\n", strerror(errno));
        free(req);
        return NULL;
    }

    if (bytes_received != sizeof(struct request)) {
        fprintf(stderr, "Incomplete packet (%zd bytes)\n", bytes_received);
        free(req);
        return NULL;
    }
    return req;
}

/* Send an answer to the last known client address */
int sendAnswer(struct answer *answerPtr)
{
    if (!answerPtr) return -1;
    size_t result = sendto(gl_sockfd, (const void *)answerPtr, sizeof(struct answer), 0, (struct sockaddr *)&client_addr, client_addr_len);
    return (result == sizeof(struct answer)) ? 0 : -1;
}

/* Close server socket and reset state */
int exitServer(void)
{
    close(gl_sockfd);
    gl_sockfd = -1;
    memset(&client_addr, 0, sizeof(client_addr));
    expected_seq = 0;
    helloDone = 0;
    closeDone = 0;
    server_running = 0;

    return 0;
}

/* --------------------------------------------------------------- */
/*  ARQ/GBN receiver logic                                         */
/* --------------------------------------------------------------- */

static struct answer *processRequest(struct request *reqPtr,
                                     struct answer *answPtr,
                                     double lossReq)
{
    if (!reqPtr || !answPtr) return NULL;

    /* Simulate request packet loss */
    if (lossReq > 0.0) {
        double randVal = (double)rand() / RAND_MAX;
        if (randVal < lossReq) {
            return NULL;
        }
    }

    switch (reqPtr -> ReqType) {

        case ReqHello: {
            if (!helloDone) {
                expected_seq = 0;
                closeDone = 0;
                if (gl_appStartFn() < 0) {
                    answPtr->AnswType = AnswErr;
                    answPtr->ErrNo = ERR_FILE_ERROR;
                    return answPtr;
                }
                helloDone = 1;
            }

            answPtr->AnswType = AnswHello;
            answPtr->SeNo = expected_seq;
            return answPtr;
        }

        case ReqData: {
            if (!helloDone) {
                answPtr->AnswType = AnswErr;
                answPtr->ErrNo = ERR_ILLEGAL_REQUEST;
                return answPtr;
            }

            if (reqPtr->SeNr == expected_seq) {
                printf("[RECV] correct SeqNr=%lu\n", expected_seq);
                if (reqPtr->FlNr > BufferSize) {
                    answPtr->AnswType = AnswErr;
                    answPtr->ErrNo = ERR_ILLEGAL_REQUEST;
                    return answPtr;
                }
                if (gl_appWriteFn(reqPtr->name, reqPtr->FlNr) < 0) {
                    answPtr->AnswType = AnswErr;
                    answPtr->ErrNo = ERR_FILE_ERROR;
                    return answPtr;
                }
                expected_seq++;
            } else {
                printf("[RECV] out-of-order SeqNr=%lu, expected=%lu\n", reqPtr->SeNr, expected_seq);
            }
            answPtr->AnswType = AnswOk;
            answPtr->SeNo = expected_seq;
            return answPtr;
        }

        case ReqClose:
            if (!helloDone) {
                if (closeDone) {
                    answPtr->AnswType = AnswOk;
                    answPtr->SeNo = expected_seq;
                    server_running = 0;
                    return answPtr;
                }
                answPtr->AnswType = AnswErr;
                answPtr->ErrNo = ERR_ILLEGAL_REQUEST;
                return answPtr;
            }

            printf("detected case: ReqClose\n");
            gl_appEndFn();
            helloDone = 0;
            closeDone = 1;
            answPtr->AnswType = AnswOk;
            answPtr->SeNo = expected_seq;
            server_running = 0;
            return answPtr;

        default:
            printf("invalid Request Type \n");
            answPtr->AnswType = AnswErr;
            answPtr->ErrNo = ERR_ILLEGAL_REQUEST;
            return answPtr;
        }
    return NULL;
}

/* --------------------------------------------------------------- */
/*  ARQ server main loop                                           */
/* --------------------------------------------------------------- */

int arqServerLoop(const char *port,
                  double lossReq,
                  double lossAck,
                  appStartFn appStart,
                  appWriteFn appWrite,
                  appEndFn appEnd)
{
    gl_appStartFn = appStart;
    gl_appWriteFn = appWrite;
    gl_appEndFn = appEnd;

    if (initServer(port) != 0) {
        return -1;
    }

    while (1) {
        struct request* req = getRequest();
        if (req == NULL) continue;

        struct answer answ = { 0 };
        struct answer* result = processRequest(req, &answ, lossReq);

        /* Simulate ACK loss */
        if ((((double)rand() / (double)RAND_MAX) >= lossAck) && result != NULL) {
            int sentAnsw = sendAnswer(result);
            if(sentAnsw != 0) {
                printf("Sent ACK, result: %d\n", sentAnsw);
            } else {
                fprintf(stderr, "Socket failed to send\n");
            }
        }

        free(req);
        if (!server_running) {
            exitServer();
            break;
        }
    }
    return 0;
}

#ifndef SERVERSY_H_INCLUDED
#define SERVERSY_H_INCLUDED

#include "data.h"

/*
 * Application callbacks:
 * The ARQ layer invokes these to pass received data to the
 * application (server.c).
 */

/* Called at transfer start (e.g. open output file). Returns 0 on success. */
typedef int  (*appStartFn)(void);

/* Write payload to the application (e.g. to file). Returns 0 on success. */
typedef int  (*appWriteFn)(const char *buf, unsigned long len);

/* Called at transfer end (e.g. close file). */
typedef void (*appEndFn)(void);


/* SAP functions — UDP layer */
int initServer(const char *port);
struct request *getRequest(void);
int sendAnswer(struct answer *answerPtr);
int exitServer(void);

/*
 * ARQ server main loop:
 *   - Receives requests over UDP
 *   - Executes ARQ logic
 *   - Invokes callbacks for in-order data packets
 *
 * lossReq / lossAck: packet and ACK loss probability (0.0-1.0)
 */
int arqServerLoop(const char *port,
                  double lossReq,
                  double lossAck,
                  appStartFn appStart,
                  appWriteFn appWrite,
                  appEndFn appEnd);

#endif /* SERVERSY_H_INCLUDED */

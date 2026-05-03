#ifndef CLIENTSY_H
#define CLIENTSY_H

#include "data.h"

/*
 * ARQ Client API
 *
 * Called by the application layer (client.c).
 * The implementation in clientSy.c encapsulates:
 *   - UDP transport (socket, send/recv)
 *   - ARQ protocol (windowing, timers, retransmits)
 */

/* Initialize UDP socket and connect to server */
void initClient(char *name, const char *port);

/* Close socket and reset client state */
void closeClient(void);

/* Send Hello, wait for server acknowledgement.
 * Returns 0 on success, non-zero on error.
 */
int arqSendHello(int winSize);

/* Reliably transfer one app_unit to the server.
 * Returns 0 on success, non-zero on error.
 */
int arqSendData(const struct app_unit *app, int winSize);

/* Gracefully close the connection (drain + Close/ACK).
 * Returns 0 on success, non-zero on error.
 */
int arqSendClose(int winSize);

#endif /* CLIENTSY_H */

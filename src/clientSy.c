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


//globale Variablen
static int gl_sockfd; //socket FileDescriptor
static unsigned long seq_num = 0; //Sequenznummer
static unsigned long base = 0; //base = kleinste unbestätigte Sequenznummer
static unsigned int timeout_counter = 0; //timout counter

//Ringpuffer
struct buf {
    char data[BufferSize]; // Das Paket = Nutzdaten
    unsigned long len; // Länge der Nutzdaten
};
struct buf buffer[GBN_BUFFER_SIZE];

/*
 * clientSy.c
 *
 * Diese Datei soll die ARQ-/Go-Back-N-Sende- und das UDP Handling
 * für den Client enthalten.
 *
 * WICHTIG:
 *   - Dateiname und Funktionssignaturen in clientSy.h sollen
 *     unverändert beibehalten werden.
 *
 * Aufgaben:
 *   - UDP-Socket (IPv6, Datagram) erzeugen, Serveradresse auflösen
 *   - ARQ-/GBN-Sendealgorithmus implementieren:
 *        * Fensterverwaltung (base, seq_num, packetCount)
 *        * innerhal eines Intervalls max. 1 Paket senden und empfangen (GBN_TIMEOUT_INT_MS)
 *        * Paket-Timeout in Intervallen (GBN_TIMEOUT_UNITS)
 *        * Retransmission der unbestätigten Pakete
 *   - kumulative ACKs (AnswOk.SeNo) auswerten
 *   - Hello/Data/Close über die gemeinsame Logik abwickeln
 */

/* Optionale globale Variablen:
 *   - Socket-Deskriptor
 *   - Serveradresse
 *   - GBN-Sendezustand (Fensterbasis, nächster freier Platz etc.)
 */

/* --------------------------------------------------------------- */
/*  Initialisierung / Abschluss                                   */
/* --------------------------------------------------------------- */
 
void initClient(char *name, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *res, *rp;
    int ret;
    // getaddrinfo Konfiguration
    memset(&hints, 0, sizeof(hints)); // alle bytes 0 setzen
    hints.ai_family   = AF_INET6;      // IPv6
    hints.ai_socktype = SOCK_DGRAM;    // UDP

    //mit getaddrinfo(...) die Serveradresse ermitteln
    ret = getaddrinfo(name, port, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfoa: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {

        //Socket in globaler Variable speichern
        gl_sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol); //UDP/IPv6-Socket erzeugen
        if (gl_sockfd == -1)
            continue;

        // Socket auf non-blocking setzen (fcntl)
        int flags = fcntl(gl_sockfd, F_GETFL, 0);
        fcntl(gl_sockfd, F_SETFL, flags | O_NONBLOCK);

        // "connect" für UDP erlaubt (setzt Default-Ziel) => im späteren Code nur send() und recv() beziehen sich auf Adresse zu der "connected" wurde
        if (connect(gl_sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        // schließe Socket
        close(gl_sockfd);
        gl_sockfd = -1;
    }

    freeaddrinfo(res); // Addresse löschen
    // Fehlermeldung 
    if (gl_sockfd == -1) {
        fprintf(stderr, "Could not connect to server\n");
        exit(EXIT_FAILURE);
    }
}

void closeClient(void)
{
    //ggf. offenen Socket schließen
    if (gl_sockfd != -1 && gl_sockfd != 0) {
        close(gl_sockfd);
    }
    //globale Zustandsvariablen zurücksetzen
    gl_sockfd = -1;
    printf("Client efolfreich geschlossen");
}

/* --------------------------------------------------------------- */
/*  Interne Hilfsfunktionen                                        */
/* --------------------------------------------------------------- */
/*
 * warteIntervallRest:
 *   Wartet die verbleibende Zeit bis zum Intervallende.
 */
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
/*  Interne Sende-Logik (GBN/ARQ)                                  */
/* --------------------------------------------------------------- */
/*
 * doRequest - Kernfunktion des Go-Back-N Protokolls
 *
 * Parameter:
 *   req          : Request-Paket (Hello/Data/Close)
 *   winSize      : Fenstergröße (1..GBN_MAX_WINDOW)
 *   windowFull   : Rückgabe: 1 wenn Sendefenster voll
 *   retransmission: Rückgabe: 1 wenn Retransmit durchgeführt
 *
 * Rückgabe: Zeiger auf Antwort oder NULL wenn keine Antwort
 */
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
        int gesendet = 0;

        retransmit_active = 0;
        retransmit_seq = 0;

        while (1) {
            struct timeval start;
            gettimeofday(&start, NULL);

            // Senden: Erstes Mal oder nach Timeout (3 Intervalle)
            if (!gesendet || hc_timeout_counter >= GBN_TIMEOUT_UNITS) {
                send(gl_sockfd, req, sizeof(*req), 0);
                gesendet = 1;
                hc_timeout_counter = 0;
            }

            // Auf ACK warten (max. 1 Intervall) 
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(gl_sockfd, &rfds);
            struct timeval timeout = { 0, interval_us };

            if (select(gl_sockfd + 1, &rfds, NULL, NULL, &timeout) > 0) {
                if (recv(gl_sockfd, &ans, sizeof(ans), 0) == sizeof(ans)) {
                    // Gültige Antwort?
                    int gueltig = (ans.AnswType == AnswOk || ans.AnswType == AnswErr);
                    if (req->ReqType == ReqHello && ans.AnswType == AnswHello)
                        gueltig = 1;

                    if (gueltig) {
                        warteIntervallRest(&start, interval_us);
                        return &ans;
                    }
                }
            }

            /* Kein ACK - warten bis Intervallende, dann Timeout-Zähler erhöhen */
            warteIntervallRest(&start, interval_us);
            hc_timeout_counter++;
        }
    }

    // DATA - Go-Back-N Algorithmus
    struct timeval start;
    gettimeofday(&start, NULL);

    // Retransmit-Zustand prüfen: Alle Pakete bestätigt?
    if (base == seq_num)
        retransmit_active = 0;

    // Timeout erreicht? Go-Back-N ab base starten
    if (base < seq_num && timeout_counter >= GBN_TIMEOUT_UNITS) {
        printf("[TIMEOUT] Starte Retransmit ab SeqNr=%lu (bis %lu)\n", base, seq_num - 1);
        retransmit_active = 1;
        retransmit_seq = base;
        timeout_counter = 0;
    }

    // ----- SENDEN: Retransmit hat Vorrang vor neuen Paketen -----
    if (retransmit_active && retransmit_seq < seq_num) {
        // Paket aus Ringpuffer laden und erneut senden
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
        // Fenster prüfen: seq_num < base + winSize
        if (seq_num >= base + (unsigned long)winSize ||
            (seq_num - base) >= GBN_BUFFER_SIZE) {
            printf("Window FULL\n");
            if (windowFull) *windowFull = 1;
        } else {
            // Neues Paket in Ringpuffer speichern und senden
            unsigned int idx = seq_num % GBN_BUFFER_SIZE;
            buffer[idx].len = req->FlNr;
            memcpy(buffer[idx].data, req->name, req->FlNr);
            req->SeNr = seq_num;
            send(gl_sockfd, req, sizeof(*req), 0);
            printf("[SEND] SeqNr=%lu\n", seq_num);
            seq_num++;
        }
    }

    // ----- EMPFANGEN: Auf kumulative ACK warten -----
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(gl_sockfd, &rfds);
    struct timeval timeout = { 0, interval_us };

    if (select(gl_sockfd + 1, &rfds, NULL, NULL, &timeout) > 0) {
        if (recv(gl_sockfd, &ans, sizeof(ans), 0) == sizeof(ans)) {
            if (ans.AnswType == AnswOk) {
                unsigned long ack_no = ans.SeNo;  // Kumulative ACK = nächste erwartete SeqNr

                // ACK im gültigen Fensterbereich?
                if (ack_no < base || ack_no > seq_num) {
                    printf("[DROP] ACK außerhalb Fenster: %lu\n", ack_no);
                } else {
                    ret = &ans;
                    if (ack_no > base) {
                        base = ack_no;  // Fenster weiterschalten
                        timeout_counter = 0;

                        // Retransmit-Position anpassen falls nötig
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

    // Intervallende abwarten (zeitsynchrones System)
    warteIntervallRest(&start, interval_us);

    // ----- TIMER: Timeout-Zähler aktualisieren -----
    if (base < seq_num) {
        // Pakete ausstehend: Zähler erhöhen wenn kein Fortschritt
        timeout_counter = (base == prev_base) ? timeout_counter + 1 : 0;
    } else {
        timeout_counter = 0;
    }

    return ret;
}

int arqSendHello(int winSize)
{
    memset(buffer, 0, sizeof(buffer)); //alle Werte im Ringpuffer auf 0 setzten
    seq_num = 0;
    base = 0;
    timeout_counter = 0;
    struct request hello; //struct request für Hello vorbereiten (ReqType = ReqHello)
    memset(&hello, 0, sizeof(struct request)); //set all struct values 0
    
    //Sequenznummern- und Fensterzustand initialisieren
    hello.ReqType = ReqHello;
    hello.SeNr = 0; //Sequence Nr
    hello.FlNr = 0;
    int window_full = 0;
    int retransmission = 0;
    
    struct answer *response = doRequest(&hello, winSize, &window_full,&retransmission); //doRequest(...) aufrufen
    if (!response) {
        printf("Hello timeout - no response\n");
        return -1;  //bei Fehler einen Wert != 0 zurückgeben
    }
    switch (response->AnswType) {   //Antwort auswerten (AnswHello / AnswOk)
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
    return -1; //bei Fehler einen Wert != 0 zurückgeben
}

int arqSendData(const struct app_unit *app, int winSize)
{
    //aus app eine struct request mit ReqType = ReqData erzeugen
    struct request data;
    memset(&data, 0, sizeof(data)); //alle Werte auf 0 setzen

    data.ReqType = ReqData; //Nutzdaten
    data.SeNr = seq_num; //SeNr = laufende Paketnummer (z.B. statischer Zähler)
    data.FlNr = app->len > BufferSize ? BufferSize : app->len; //FlNr = app->len (ggf. begrenzen auf BufferSize)

    memcpy(data.name, app->data, data.FlNr); //Nutzdaten kopieren

    //erstmal davon ausgehen das Fenster frei ist und keine Retransmission notwendig
    int window_full = 0;
    int retransmission = 0;

    struct answer* response = doRequest(&data, winSize, &window_full, &retransmission); // doRequest(...) aufrufen

    //bei bufferFull oder retransmission doRequest mit unverändertem req erneut aufrufen
    if (window_full || retransmission) {
        return 1;   // signalisiert main(): nochmal probieren:
    }
    if (response && response->AnswType == AnswErr) {
        printf("Server Error (ErrNo=%lu)\n", response->ErrNo);
        return -1; // signalisiert main(): nochmal probieren:
    }

    return 0; // Paket wurde gesendet/übernommen
}

int arqSendClose(int winSize)
{
    //struct request mit ReqClose vorbereiten
    struct request close;
    memset(&close, 0, sizeof(close));
    close.ReqType = ReqClose;
    close.SeNr = seq_num; //Sequence Nr
    close.FlNr = 0;
    // mehrfaches Senden des Close um Verlust zu vermeiden
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
            fprintf(stderr, "Client: error beim löschen des Buffers (ErrNo=%lu).\n", resp->ErrNo);
            return -1;
        }
    }

    int window_full = 0;
    int retransmission = 0;
    struct answer* response = doRequest(&close, winSize, &window_full, &retransmission);
    if (!response) {
        fprintf(stderr, "Client: close timeout, verlässt.\n");
        return -1;
    }
    if (response->AnswType == AnswOk) {
        printf("Server AnswOk   - Verbindung geschlossen\n");
        return 0;
    }
    if (response->AnswType == AnswErr) {
        printf("Server Error (ErrNo=%lu)\n", response->ErrNo);
        return -1;
    }
    return -1;
}

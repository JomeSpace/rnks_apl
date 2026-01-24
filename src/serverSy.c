/* serverSy.c - UDP + ARQ/Go-Back-N-Server mit Loss-Simulation
 *
 * Schichten:
 *   - SAP-Schicht (UDP): initServer, getRequest, sendAnswer, exitServer
 *   - ARQ-Schicht: processRequest(), arqServerLoop()
 *
 * Die Anwendung (Datei öffnen/schreiben/schließen) wird über Callbacks
 * aus server.c angebunden:
 *   appStartFn  appStartTransfer
 *   appWriteFn  appWriteData
 *   appEndFn    appEndTransfer
 *
 * WICHTIG:
 *   - Dateiname und Funktionssignaturen in serverSy.h sollen
 *     unverändert beibehalten werden.
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

/* Optionale globale Variablen:
 *   - Socket-Deskriptor
 *   - zuletzt bekannte Client-Adresse (für sendto)
 *   - nächst erwartete Sequenznummer (expected_seq)
 *   - Zeiger auf die Anwendungscallbacks
 * 
 */
//globale Variablen

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
/*  SAP-Schicht (UDP)                                              */
/* --------------------------------------------------------------- */

int initServer(const char *port)
{
    /* TODO:
     *  - mit getaddrinfo(NULL, port, ...) eine lokale Adresse für UDP/IPv6
     *    ermitteln
     *  - mit socket(...) einen UDP/IPv6-Socket erzeugen
     *  - mit bind(...) an die Adresse binden
     *  - Socket-Deskriptor in globaler Variable speichern
     *  - bei Erfolg 0, bei Fehler <0 zurückgeben
     */
    struct addrinfo hints;
    struct addrinfo* res, * tmp;
    int sockfd = -1;

    // getaddrinfo einstellungen
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = DEFAULT_FAMILY; // Auswahl des IP Standards
    hints.ai_socktype = DEFAULT_SOCKTYPE; // festlegung Socket Typ z.B. UDP, TCP ...
    hints.ai_flags = AI_PASSIVE; // Einstellung für getaddrinfo: valide IP Addressen und Interfaces
    // Fordere Valide IP Addressen mit angegeben Parameter z.B. Port
    int ret = getaddrinfo(NULL, port, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }

    // erste valide Adresse nehmen
    for (tmp = res; tmp != NULL; tmp = tmp->ai_next) {
        // initalisiere Socket Einstellung
        sockfd = socket(tmp->ai_family, tmp->ai_socktype, tmp->ai_protocol);
        if (sockfd >= 0) {
            if (bind(sockfd, tmp->ai_addr, tmp->ai_addrlen) == 0) {
                gl_sockfd = sockfd;
                freeaddrinfo(res);
                printf("completed: initServer() from arqServerLoop()\n");
                return 0;
            }
            close(sockfd); // Socket schließen
        }
    }
    // Fehlermeldung unfähig Socket zu erstellen
    fprintf(stderr, "Server: Socket binden ist fehlgeschlagen\n");
    freeaddrinfo(res);
    if (sockfd >= 0) {
        close(sockfd);
    }
    return -1;
}

struct request *getRequest(void)
{
    /* TODO:
     *  - mit recvfrom(...) ein Request-Paket vom Socket lesen
     *  - die Adresse des Clients (struct sockaddr_storage) merken,
     *    damit sendAnswer() dorthin antworten kann
     *  - bei Erfolg &req zurückgeben
     *  - bei Fehler oder wenn keine Daten vorliegen: NULL zurückgeben
     */
    // definiere req struct und sichere Speicherbereich
    struct request *req = malloc(sizeof(struct request));
    if (!req) return NULL;

    socklen_t addr_len = sizeof(client_addr);
    ssize_t bytes_received;

    bytes_received = recvfrom(gl_sockfd, req, sizeof(struct request), 0,(struct sockaddr*)&client_addr, &addr_len); // erhalte request von socket
    client_addr_len = addr_len;
    // Fehlermeldung für nicht erhaltenen Req
        if (bytes_received == -1) {
        printf("recvfrom error: %s\n", strerror(errno));
        free(req);
        return NULL;
    }

    // Überprüfe auf vollständiges Packet
    if (bytes_received != sizeof(struct request)) {
        fprintf(stderr, "Incomplete packet (%zd bytes)\n", bytes_received);
        free(req);
        return NULL;
    }
    return req;
}

int sendAnswer(struct answer *answerPtr)
{
    /* TODO: DONE
     *  - mit sendto(...) eine Antwort an die zuletzt bekannte
     *    Client-Adresse schicken
     *  - bei Erfolg 0, bei Fehler <0 zurückgeben
     */

    if (!answerPtr) return -1;  // Null pointer Überprüfung
    size_t result = sendto(gl_sockfd,(const void *)answerPtr, sizeof(struct answer), 0, (struct sockaddr *)&client_addr, client_addr_len); // sende Ack mit Socket
    return (result == sizeof(struct answer)) ? 0 : -1;  // 0=success, -1=error
}

int exitServer(void)
{
    /* TODO:
     *  - Socket schließen (close)
     *  - globale Zustandsvariablen zurücksetzen
     */

    close(gl_sockfd); // schließe Socket
    // setze Konstanten zurück
    gl_sockfd = -1;
    memset(&client_addr, 0, sizeof(client_addr)); // lösche Client Addresse
    expected_seq = 0;
    helloDone = 0;
    closeDone = 0;
    server_running = 0;

    return 0;
}

/* --------------------------------------------------------------- */
/*  ARQ-/GBN-Logik (Empfänger)                                     */
/* --------------------------------------------------------------- */

/*
 * processRequest:
 *  - nimmt ein Request-Paket entgegen
 *  - führt die ARQ-/GBN-Empfangslogik aus
 *  - erzeugt eine passende Antwort (ACK/Fehler)
 *
 *   ReqHello:
 *     - Sequenznummernzustand initialisieren (z.B. expected_seq = 0)
 *     - Anwendung per appStartFn informieren
 *     - eine passende Antwort (AnswHello/AnswOk) eintragen
 *
 *   ReqData:
 *         * ggf. Nutzdaten an appWriteFn übergeben
 *         * (kumulatives) ACK senden 
 *       
 *   ReqClose:
 *     - appEndFn aufrufen
 *     - Abschluss-ACK senden
 *
 * lossReq:
 *   - simulierte Paketverlustrate für Requests (0.0..1.0)
 *     (z.B. über Zufallszahlvergleich ein Paket "fallen lassen")
 *
 * Rückgabewert:
 *   - Zeiger auf ausgefüllte Antwortstruktur (answPtr)
 *   - NULL, wenn das Request-Paket vollständig verworfen wurde
 */
static struct answer *processRequest(struct request *reqPtr,
                                     struct answer *answPtr,
                                     double lossReq)
{
    /* TODO: Done
     *  - Paketverlust über lossReq simulieren
     *  - je nach ReqType (ReqHello / ReqData / ReqClose) handeln
     *  - Antwort (AnswType, SeNo/ErrNo) setzen
     *  - bei verworfenem Request NULL zurückgeben
     */

    if (!reqPtr || !answPtr) return NULL;

    // Simuliere Packet Verlust mit Zufallszahl bzw Wahrscheinlichkeit (z.B.:, 0.01 = 1%)
    if (lossReq > 0.0) {
        double randVal = (double)rand() / RAND_MAX;
        if (randVal < lossReq) {
            return NULL;  // Entspricht Packet Verloren
        }
    }
    // Request Typen Handhabung:
    switch (reqPtr -> ReqType) {
        // Hello Handshake:
        case ReqHello: {
            if (!helloDone) {
                expected_seq = 0;
                closeDone = 0;
                // Aufruf gl_appStartFn
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
        // Data Request:
        case ReqData: {
            // error wenn Hello noch nicht gemacht
            if (!helloDone) {
                answPtr->AnswType = AnswErr;
                answPtr->ErrNo = ERR_ILLEGAL_REQUEST;
                return answPtr;
            }
            // überorüfe ob erwartete seq Nummer erhaltener Entspricht
            if (reqPtr->SeNr == expected_seq) {
                printf("[RECV] correct SeqNr=%lu\n", expected_seq);
                // Illegale Request Error im Fall einer zu großen Request Size
                if (reqPtr->FlNr > BufferSize) {
                    answPtr->AnswType = AnswErr;
                    answPtr->ErrNo = ERR_ILLEGAL_REQUEST;
                    return answPtr;
                }
                // In Datei schreiben mit optionalen File Error
                if (gl_appWriteFn(reqPtr->name, reqPtr->FlNr) < 0) {
                    answPtr->AnswType = AnswErr;
                    answPtr->ErrNo = ERR_FILE_ERROR;
                    return answPtr;
                }
                expected_seq++; // erhöhung der Erwarteten Packnummer
            } else { // Return im Fall einer außerordentlichen Request
                printf("[RECV] out-of-order SeqNr=%lu, expected=%lu\n", reqPtr->SeNr, expected_seq);
            }
            answPtr->AnswType = AnswOk;
            answPtr->SeNo = expected_seq;
            return answPtr;
        }
        // Close Request:
        case ReqClose:
            // Im Fall einer Close Request bevor eine HELLO Request Empfangen wurde
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
            // Standart Close Answer
            printf("detected case: ReqClose\n");
            gl_appEndFn();
            helloDone = 0;
            closeDone = 1;
            answPtr->AnswType = AnswOk;
            answPtr->SeNo = expected_seq;
            server_running = 0;
            return answPtr;

        default: // Generischer Fehler im Falle eines Unbekannten Req Types
            printf("invalid Request Type \n");
            answPtr->AnswType = AnswErr;
            answPtr->ErrNo = ERR_ILLEGAL_REQUEST;
            return answPtr;
        }
    return NULL;
}

/* --------------------------------------------------------------- */
/*  ARQ-Server-Hauptschleife                                       */
/* --------------------------------------------------------------- */

int arqServerLoop(const char *port,
                  double lossReq,
                  double lossAck,
                  appStartFn appStart,
                  appWriteFn appWrite,
                  appEndFn appEnd)
{
    /* TODO: partiallyDone
     *  - Callbacks in globalen Variablen speichern oder für Weiterreichen 
	 *    Parameterliste in processRequest erweitern DONE (global appr)
     *  - initServer(port) aufrufen
     *  - Sequenznummerzustand initialisieren (z.B. expected_seq = 0)
     *  - Endlosschleife:
	 *		 * Paketverlust mit lossReq simulieren
     *       * ggf. getRequest() aufrufen
     *       * processRequest(..) aufrufen
     *       * ACK-Verlust mit lossAck simulieren
     *       * bei "kein ACK-Verlust": sendAnswer(..) aufrufen
     *  - exitServer() im Fehlerfall oder bei Beenden verwenden
     */
    // definierung der Globalen Datei-Handhabungsfunktione
    gl_appStartFn = appStart;
    gl_appWriteFn = appWrite;
    gl_appEndFn = appEnd;

    // initalisierung des Sockets:
    if (initServer(port) != 0) {
        return -1;
    }    

    // Main Server Listen Loop
    while (1) {
        // Request erhalten und überprüfen
        struct request* req = getRequest();
        if (req == NULL) continue;

        // Request verarbeiten und Answ deklarieren
        struct answer answ = { 0 };
        struct answer* result = processRequest(req, &answ, lossReq);

        // simuliere ACK Verlust
        if ((((double)rand() / (double)RAND_MAX) >= lossAck) && result != NULL) {
            int sentAnsw = sendAnswer(result);
            if(sentAnsw != 0) { // sende Ack and Client
                printf("Sent ACK, result: %d\n", sentAnsw);
            } else {
                fprintf(stderr, "Socket failed to sent\n");
            }
        }
        
        free(req); // request inhalt löschen
        // Server herunterfahren wenn server runnning flag 0
        if (!server_running) {
            exitServer(); // Globale Standard Werte zutücksetzen und Socket schließen
            break;
        }
    }
    return 0;
}

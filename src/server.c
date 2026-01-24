#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "serverSy.h"

/* Anwendungszustand: Ausgabedatei */
static const char *gOutputFile = NULL;
static FILE       *gFp         = NULL;
static int         gFileOk     = 0;

static void usage(const char *progName)
{
    fprintf(stderr, "Usage: %s -p <port> -f <outfile> [-r <lossReq>] [-a <lossAck>]\n",
            progName);
    fprintf(stderr, "   -p <port>    : Server-Port (Default: %s)\n", DEFAULT_PORT);
    fprintf(stderr, "   -f <outfile> : Ausgabedatei\n");
    fprintf(stderr, "   -r <lossReq> : Request-Verlustwahrscheinlichkeit (0.0..1.0)\n");
    fprintf(stderr, "   -a <lossAck> : ACK-Verlustwahrscheinlichkeit (0.0..1.0)\n");
    exit(EXIT_FAILURE);
}

/* Anwendungscallbacks für die ARQ-Schicht */

/* Ausgabedatei öffnen/neu anlegen. */
static int appStartTransfer(void)
{   
    gFileOk = 0;

    if (!gOutputFile) {
        fprintf(stderr, "Server: no output file specified.\n");
        return -1;
    }

    gFp = fopen(gOutputFile, "w"); //Datei gOutputFile zum Schreiben öffnen; Zeiger in gFp ablegen

    if (gFp == NULL) {
        fprintf(stderr, "Server: Fehler beim Öffnen von '%s'\n", gOutputFile);
        return -1; //bei Fehler Fehlermeldung ausgeben und <0 zurückgeben
    }
    else
    {
        gFileOk = 1; //bei Erfolg gFileOk = 1 setzen
        printf("Server: start transfer -> writing to '%s'\n", gOutputFile);
        return 0;
    }
}

/* Nutzdaten in Datei schreiben. */
static int appWriteData(const char *buf, unsigned long len)
{
    // prüfen, ob gFileOk und gFp gültig sind
    if (gFileOk != 1 || gFp == NULL) {
        return -1;
    }
    // len Bytes aus buf in gFp schreiben (fwrite)
    size_t written = fwrite(buf, 1, len, gFp);
    if (written != (size_t)len) {
        return -1; //bei Fehler <0 zurückgeben
    }
    
    return 0; //bei Erfolg 0 zurückgeben
}

/* Datei schließen. */
static void appEndTransfer(void)
{
    if (gFp != NULL) {
        fflush(gFp);
        fclose(gFp); //falls gFp != NULL: Datei schließen (fclose)
    }
    gFp = NULL; //gFp auf NULL setzen
    gFileOk = 0; //gFileOk zurücksetzen
}

/* --- main: Argumente auswerten, ARQ-Schicht starten --- */

int main(int argc, char *argv[])
{
    const char *port = DEFAULT_PORT;
    double lossReq   = 0.0;
    double lossAck   = 0.0;
    long i;

    /* Programmargumente auswerten */
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                switch (tolower((unsigned char)argv[i][1])) {

                case 'p': /* Server-Port */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'f': /* Ausgabedatei */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        gOutputFile = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'r': /* Request-Verlust */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossReq = atof(argv[++i]);
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'a': /* ACK-Verlust */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossAck = atof(argv[++i]);
                        break;
                    }
                    usage(argv[0]);
                    break;

                default:
                    usage(argv[0]);
                    break;
                }
            } else {
                usage(argv[0]);
            }
        }
    }
    // Überprüfe ob standart OutputFile ist definiert sonst Programmargument nutzen
    if (!gOutputFile) {
        usage(argv[0]);
    }

    printf("Server: listening on port %s\n", port);
    printf("Server: lossReq = %f, lossAck = %f\n", lossReq, lossAck);

    // stare ARQ Logik aus serverSy
    if (arqServerLoop(port, lossReq, lossAck,
                      appStartTransfer, appWriteData, appEndTransfer) < 0) {
        fprintf(stderr, "Server: arqServerLoop failed\n");
        return EXIT_FAILURE;
    }
    printf("Server erfolgreich geschlossen\n");
    return EXIT_SUCCESS;
}

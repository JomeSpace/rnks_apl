#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "clientSy.h"

/* usage-Ausgabe */
static void usage(const char *progName)
{
    fprintf(stderr, "Usage: %s -a <server> -p <port> -f <file> -w <window>\n", progName);
    fprintf(stderr, "       -a <server> : Server-Adresse (Default: %s)\n",
            (DEFAULT_SERVER == NULL) ? "loopback" : DEFAULT_SERVER);
    fprintf(stderr, "       -p <port>   : Server-Port (Default: %s)\n", DEFAULT_PORT);
    fprintf(stderr, "       -f <file>   : Eingabedatei\n");
    fprintf(stderr, "       -w <window> : Fenstergröße (1..10)\n");
    exit(EXIT_FAILURE);
}
//liest eine Zeile oder BufferSize viele Zeichen aus der Input-Datei und speichert diese in App->Data
static int readAppUnit(struct app_unit *app, FILE *f)
{
    char Buffer[BufferSize] = { '\0' }; // setze alle Werte auf '\0'

    int i = 0;
    while (i < BufferSize)  // Limitiere Buffergröße 
    {
        int c = fgetc(f);   // einlesen char für char
        // Datei Fehler Handhabung
        if (ferror(f)) {
            return -1; 
        }
        // Überprüfe auf Dateiende
        if (c == EOF) { 
            if (i != 0) {
                break;// partielles Daten einlesen vor dem EOF (Dateiende)
            }
            else {
                return 0;
            }
        }
        Buffer[i] = (char)c; // kopiere Zeile in den Buffer
        i++; 
        
        if (c == '\n') {    // Halte am Zeilenumbruch ('\n')
            break;
        }
    }

    memcpy(app->data, Buffer, BufferSize); // kopiere den Bufferinhalt in den app->data Struct
    app->len = strlen(app->data); // länge des Struct setzen bzw. ermitteln
    return 1; //return SUCCESS
}

int main(int argc, char *argv[])
{
    const char *server     = DEFAULT_SERVER;
    const char *filename   = NULL;
    const char *port       = DEFAULT_PORT;
    const char *windowSize = "1";

    FILE *fp = NULL;
    long i;

    /* Kommandozeilenparameter auswerten */
    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                switch (tolower((unsigned char)argv[i][1])) {

                case 'a': /* Server-Adresse */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        server = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'p': /* Server-Port */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'f': /* Eingabedatei */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        filename = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'w': /* Fenstergröße */
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        windowSize = argv[++i];
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

    if (!filename) {
        usage(argv[0]);
    }

    if (atoi(windowSize) < 1 || atoi(windowSize) > GBN_MAX_WINDOW) {
        usage(argv[0]);
    }

    fp = fopen(filename, "r");  //Datei filename zum Lesen öffnen; FILE* in fp ablegen
    if (fp == NULL) {           //bei Fehler: perror / Fehlermeldung und EXIT_FAILURE
        perror("Fehler beim öffnen der Datei");
        return EXIT_FAILURE;
    }
    printf("Client: sending file '%s'\n", filename);

    /* ARQ-Client initialisieren */
    initClient((char *)server, port);

    /* Hello/Verbindungsaufbau */
    if (arqSendHello(atoi(windowSize)) != 0) {  // !=0 entspricht hier dem Fehlerfall
        fprintf(stderr, "Client: Hello failed, aborting.\n");
        if (fp != NULL) { //Datei schließen, falls sie bereits geöffnet wurde
            fclose(fp);
        }
        closeClient();
        return EXIT_FAILURE;
    }

    {
        struct app_unit app_data;
        
        int result;
        while ((result = readAppUnit(&app_data, fp)) > 0) {  //Datei -> Neue Zeile lesen und als app_unit an arqSendData() übergeben 
            int send_rc;
            while ((send_rc = arqSendData(&app_data, atoi(windowSize))) != 0) { //aktuelle Zeile an arqSendData() übergeben 
                if (send_rc < 0) {
                    fprintf(stderr, "Client: error while sending data.\n");
                    fclose(fp);
                    closeClient();
                    return EXIT_FAILURE;
                }
                continue;
            }
        }
        if (result < 0) { //Fehlerfall (readAppUnit(..) < 0)
            perror("Fehler beim lesen der Datei");
            
            fclose(fp); 
            closeClient();
            return EXIT_FAILURE;    
        }
    }

    /* Close / Verbindungsabbau */
    int close_rc = arqSendClose(atoi(windowSize));
    if (close_rc != 0) {
        fprintf(stderr, "Client: close timeout, exiting.\n");
    }
    //sauberer EXIT
    if (fp != NULL) { //geöffnete Datei wieder schließen 
        fclose(fp);
    }
    closeClient();
    return (close_rc != 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "clientSy.h"

static void usage(const char *progName)
{
    fprintf(stderr, "Usage: %s -a <server> -p <port> -f <file> -w <window>\n", progName);
    fprintf(stderr, "       -a <server> : Server address (Default: %s)\n",
            (DEFAULT_SERVER == NULL) ? "loopback" : DEFAULT_SERVER);
    fprintf(stderr, "       -p <port>   : Server port (Default: %s)\n", DEFAULT_PORT);
    fprintf(stderr, "       -f <file>   : Input file\n");
    fprintf(stderr, "       -w <window> : Window size (1..10)\n");
    exit(EXIT_FAILURE);
}

static int readAppUnit(struct app_unit *app, FILE *f)
{
    char Buffer[BufferSize] = { '\0' };

    int i = 0;
    while (i < BufferSize)
    {
        int c = fgetc(f);
        if (ferror(f)) {
            return -1;
        }
        if (c == EOF) {
            if (i != 0) {
                break;
            }
            else {
                return 0;
            }
        }
        Buffer[i] = (char)c;
        i++;

        if (c == '\n') {
            break;
        }
    }

    memcpy(app->data, Buffer, BufferSize);
    app->len = strlen(app->data);
    return 1;
}

int main(int argc, char *argv[])
{
    const char *server     = DEFAULT_SERVER;
    const char *filename   = NULL;
    const char *port       = DEFAULT_PORT;
    const char *windowSize = "1";

    FILE *fp = NULL;
    long i;

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                switch (tolower((unsigned char)argv[i][1])) {

                case 'a':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        server = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'p':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'f':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        filename = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'w':
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

    fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }
    printf("Client: sending file '%s'\n", filename);

    initClient((char *)server, port);

    if (arqSendHello(atoi(windowSize)) != 0) {
        fprintf(stderr, "Client: Hello failed, aborting.\n");
        if (fp != NULL) {
            fclose(fp);
        }
        closeClient();
        return EXIT_FAILURE;
    }

    {
        struct app_unit app_data;

        int result;
        while ((result = readAppUnit(&app_data, fp)) > 0) {
            int send_rc;
            while ((send_rc = arqSendData(&app_data, atoi(windowSize))) != 0) {
                if (send_rc < 0) {
                    fprintf(stderr, "Client: error while sending data.\n");
                    fclose(fp);
                    closeClient();
                    return EXIT_FAILURE;
                }
                continue;
            }
        }
        if (result < 0) {
            perror("Error reading file");

            fclose(fp);
            closeClient();
            return EXIT_FAILURE;
        }
    }

    int close_rc = arqSendClose(atoi(windowSize));
    if (close_rc != 0) {
        fprintf(stderr, "Client: close timeout, exiting.\n");
    }

    if (fp != NULL) {
        fclose(fp);
    }
    closeClient();
    return (close_rc != 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}

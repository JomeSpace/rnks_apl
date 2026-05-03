#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "data.h"
#include "config.h"
#include "serverSy.h"

static const char *gOutputFile = NULL;
static FILE       *gFp         = NULL;
static int         gFileOk     = 0;

static void usage(const char *progName)
{
    fprintf(stderr, "Usage: %s -p <port> -f <outfile> [-r <lossReq>] [-a <lossAck>]\n",
            progName);
    fprintf(stderr, "   -p <port>    : Server port (Default: %s)\n", DEFAULT_PORT);
    fprintf(stderr, "   -f <outfile> : Output file\n");
    fprintf(stderr, "   -r <lossReq> : Request loss probability (0.0..1.0)\n");
    fprintf(stderr, "   -a <lossAck> : ACK loss probability (0.0..1.0)\n");
    exit(EXIT_FAILURE);
}

/* Application callbacks for the ARQ layer */

static int appStartTransfer(void)
{
    gFileOk = 0;

    if (!gOutputFile) {
        fprintf(stderr, "Server: no output file specified.\n");
        return -1;
    }

    gFp = fopen(gOutputFile, "w");

    if (gFp == NULL) {
        fprintf(stderr, "Server: failed to open '%s'\n", gOutputFile);
        return -1;
    }
    else
    {
        gFileOk = 1;
        printf("Server: start transfer -> writing to '%s'\n", gOutputFile);
        return 0;
    }
}

static int appWriteData(const char *buf, unsigned long len)
{
    if (gFileOk != 1 || gFp == NULL) {
        return -1;
    }
    size_t written = fwrite(buf, 1, len, gFp);
    if (written != (size_t)len) {
        return -1;
    }

    return 0;
}

static void appEndTransfer(void)
{
    if (gFp != NULL) {
        fflush(gFp);
        fclose(gFp);
    }
    gFp = NULL;
    gFileOk = 0;
}

int main(int argc, char *argv[])
{
    const char *port = DEFAULT_PORT;
    double lossReq   = 0.0;
    double lossAck   = 0.0;
    long i;

    if (argc > 1) {
        for (i = 1; i < argc; i++) {
            if (((argv[i][0] == '-') || (argv[i][0] == '/')) &&
                (argv[i][1] != 0) && (argv[i][2] == 0)) {

                switch (tolower((unsigned char)argv[i][1])) {

                case 'p':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        port = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'f':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        gOutputFile = argv[++i];
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'r':
                    if (argv[i + 1] && argv[i + 1][0] != '-') {
                        lossReq = atof(argv[++i]);
                        break;
                    }
                    usage(argv[0]);
                    break;

                case 'a':
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

    if (!gOutputFile) {
        usage(argv[0]);
    }

    printf("Server: listening on port %s\n", port);
    printf("Server: lossReq = %f, lossAck = %f\n", lossReq, lossAck);

    if (arqServerLoop(port, lossReq, lossAck,
                      appStartTransfer, appWriteData, appEndTransfer) < 0) {
        fprintf(stderr, "Server: arqServerLoop failed\n");
        return EXIT_FAILURE;
    }
    printf("Server closed successfully\n");
    return EXIT_SUCCESS;
}

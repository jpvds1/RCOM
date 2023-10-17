// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include "string.h"

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer linkLayer;
    strcpy(linkLayer.serialPort, serialPort);
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;
    if(strcmp(role, "rx") == 0)
        linkLayer.role = LlRx;
    else
        linkLayer.role = LlTx;

    if(llopen(linkLayer) != 0) return -1;
    if(linkLayer.role == LlTx)
    {
        if(llwrite() != 0) return -1;
    }
    else
    {
        if(llread() != 0) return -1;
    }
}

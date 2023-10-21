// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include "string.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#define BUF_SIZE 256

unsigned char* CtrlPacket(int size, bool start, const char* filename, int *packetSize);
//unsigned char* DataPacket(int size);

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

    if(llopen(linkLayer) != 0) return;

    
    if(linkLayer.role == LlTx)
    {
        FILE *file;
        unsigned char* file_read = NULL;
        long int bufSize;
        int packetSize;
        unsigned char* bufPacket;
        int remain_bytes;

        file = fopen(filename, "rb");
        if(file == NULL)
        {
            perror("File didnt open");
            exit(1);
        }

        fseek(file, 0L, SEEK_END);
        bufSize = ftell(file);
        fseek(file, 0L, SEEK_SET);

        file_read = (unsigned char*) malloc(bufSize);
        if(file_read == NULL)
        {
            perror("Malloc on file_read failed");
            exit(1);
        }
        remain_bytes = bufSize;

        if(fread(file_read, sizeof(unsigned char), bufSize, file) != bufSize)
        {
            perror("fread failed");
            free(file_read);
            fclose(file);
            exit(1);
        }

        bufPacket = CtrlPacket(bufSize, 1, filename, &packetSize);
        if(llwrite(bufPacket, packetSize) != 0) return;
        
        while(remain_bytes > 0)
        {
            return;
        }

        bufPacket = CtrlPacket(bufSize, 0, filename, &packetSize);
        if(llwrite(bufPacket, packetSize) != 0) return;
    }
    else
    {
        return;
        //if(llread() != 0) return 1;
    }
}

unsigned char* CtrlPacket(int size, bool start, const char* filename, int *packetSize)
{
    int v1size =(int) ceil(log2f((float) size / 8.0));
    int v2size = strlen(filename);

    unsigned char* buf = (unsigned char*) malloc(5 + v1size + v2size);

    if(start == 1)
    {
        buf[0] = 2;
    }
    else
    {
        buf[0] = 3;
    }

    buf[1] = 0;
    buf[2] = v1size;

    for(int i = 0; i < v1size; i++)
    {
        buf[3 + i] = size & 0xFF;
        size = size >> 8;
    }

    buf[3 + v1size] = 1;
    buf[4 + v1size] = v2size;

    for(int i = 0; i < v2size; i++)
    {
        buf[5 + v1size + i] = filename[i];
    }

    *packetSize = 5 + v1size + v2size;
    
    return buf;
}

/*
unsigned char* DataPacket(int size)
{
    return 'a';
}
*/

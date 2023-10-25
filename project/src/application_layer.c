// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include "string.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#define BUF_SIZE 256

unsigned char* CtrlPacket(int size, bool start, const char* filename, unsigned long int *packetSize);
unsigned char* DataPacket(int size, const unsigned char* data, unsigned long int *packetSize);
unsigned char* getData(const unsigned char* data, int size);

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    //Create the LinkLayer
    LinkLayer linkLayer;
    strcpy(linkLayer.serialPort, serialPort);
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;
    if(strcmp(role, "rx") == 0)
        linkLayer.role = LlRx;
    else
        linkLayer.role = LlTx;

    if(llopen(linkLayer) == -1) return;
    
    if(linkLayer.role == LlTx)
    {
        FILE *file;
        unsigned char* file_read = NULL;
        unsigned char* bufPacket;
        unsigned char* data;
        long int bufSize;
        unsigned long int packetSize;
        int remain_bytes;
        int dataSize;
        int index = 0;

        file = fopen(filename, "rb");
        if(file == NULL)
        {
            perror("File didnt open");
            exit(1);
        }

        //Get the size of the file
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

        //Read the file
        if(fread(file_read, sizeof(unsigned char), bufSize, file) != bufSize)
        {
            perror("fread failed");
            free(file_read);
            fclose(file);
            exit(1);
        }

        //Send the first Control Packet
        bufPacket = CtrlPacket(bufSize, 1, filename, &packetSize);
        if(llwrite(bufPacket, packetSize) == -1) return;
        free(bufPacket);
        
        while(remain_bytes > 0)
        {
            //Check the size to be sent
            if(remain_bytes > MAX_PAYLOAD_SIZE)
                dataSize = MAX_PAYLOAD_SIZE;
            else
                dataSize = remain_bytes;
            data = (unsigned char*) malloc(dataSize);
            if(data == NULL){perror("data malloc failed\n"); return;}

            //Send the Data Packet
            if(memcpy(data, file_read + index, dataSize) == NULL){return;}
            bufPacket = DataPacket(dataSize, data, &packetSize);
            if(llwrite(bufPacket, packetSize) == -1) return; 

            //Update the relevant variables
            index += dataSize;
            remain_bytes -= dataSize;
            free(bufPacket);
        }

        //Send the end Control Packet
        bufPacket = CtrlPacket(bufSize, 0, filename, &packetSize);
        if(llwrite(bufPacket, packetSize) < 0) return;
        free(bufPacket);
        free(file_read);
        free(data);
    }
    else
    {
        unsigned long int receivedSize;
        unsigned char* packetReceived = (unsigned char*) malloc(MAX_PAYLOAD_SIZE);
        if(packetReceived == NULL){perror("packetReceived malloc\n"); exit(-1);}

        //Open a file to write
        FILE *file;
        file = fopen(filename, "wb");
        while(TRUE)
        {
            //If its a Data Packet, write to the file
            receivedSize = llread(packetReceived);
            if(receivedSize < 0) {free(packetReceived); exit(-1);}
            if(packetReceived[0] == 2) continue; //First Control Packet
            if(packetReceived[0] == 3) break; //Last Control Packet
            fwrite(getData(packetReceived, receivedSize), sizeof(unsigned char), receivedSize-3, file);
        }
        free(packetReceived);
    }
    if(llclose(0) < 0) exit(-1);
}

unsigned char* CtrlPacket(int size, bool start, const char* filename, unsigned long int *packetSize)
{
    //Calculate L1 and L2
    unsigned int v1size =(int) ceil(log2f((float) size )/ 8.0);
    int v2size = strlen(filename);

    unsigned char* buf = (unsigned char*) malloc(5 + v1size + v2size);

    //Set the Packet Header
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
        buf[2 + v1size - i] = size & 0xFF;
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

unsigned char* DataPacket(int size, const unsigned char* data, unsigned long int *packetSize)
{
    unsigned char* packet = (unsigned char*) malloc(*packetSize);
    if(packet == NULL){perror("malloc on DataPacket failed\n"); return packet;}

    //Add the Packet Header for Data Packets
    packet[0] = 1;
    packet[1] = size >> 8 && 0xff;
    packet[2] = size && 0xff;
    memcpy(packet + 3, data, size);

    *packetSize = 3 + size;

    return packet;
}

unsigned char* getData(const unsigned char* data, int size)
{
    //Remove the packet header
    unsigned char* parsedData = (unsigned char*) malloc(size-3);
    memcpy(parsedData, data+3, size-3);
    return parsedData;
}

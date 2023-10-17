// Link layer protocol implementation

#include "link_layer.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256

//States (state machine)
#define START 0
#define FLAG 1
#define ADRESS 2
#define CONTROL 3
#define BCC 4
#define END 5

int fd;
struct termios oldtio;
struct termios newtio;
int alarmEnabled = TRUE;
int alarmCount = 0;
unsigned char buf_send[BUF_SIZE] = {0}; 
unsigned char buf_receive[2] = {0}; // +1: Save space for the final '\0' char
int bytess;
int STOP = FALSE;
int retries;
int timeout;

//Setup Functions
int setup(LinkLayer connectionParameters);
void alarmHandler(int signal);
//Message related functions
int send_SET();
int send_UA();
//Aux functions
int ll_open_Tx();
int ll_open_Rx();

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    fd = setup(connectionParameters);

    if(fd < 0) return 1;

    if(connectionParameters.role == LlTx)
    {
        if(ll_open_Tx()) return 1;
    }
    else
    {
        if(ll_open_Rx()) return 1;
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    return 1;
}

int setup(LinkLayer connectionParameters) //Setup the connection
{
    (void)signal(SIGALRM, alarmHandler);

    retries = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;

    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0){perror(connectionParameters.serialPort); return 1;}
    if(tcgetattr(fd, &oldtio) == -1){perror("tcgetattr"); return 1;}

    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    if(connectionParameters.role == LlRx)
    {
        newtio.c_cc[VTIME] = 0;
        newtio.c_cc[VMIN] = 1;
    }
    else
    {
        newtio.c_cc[VTIME] = 1;
        newtio.c_cc[VMIN] = 1;
    }
    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1){perror("tcsetattr"); return 1;}
    
    return fd;
}

int send_SET() //Send SET message from Tx to Rx
{
    memset(&buf_send, 0, BUF_SIZE); //Clear sending buffer

    buf_send[0] = 0x7e; //Flag = 0x7e
    buf_send[1] = 0x03; //Adress = 0x03
    buf_send[2] = 0x03; //Control = 0x03
    buf_send[3] = buf_send[1] ^ buf_send[2]; //BCC1 = Adress XOR Control
    buf_send[4] = 0x7e; //Flag = 0x7e

    write(fd, buf_send, 5);

    return 0;
}

int send_UA() //Send UA message from Rx to Tx
{
    memset(&buf_send, 0, BUF_SIZE); //Clear sending buffer

    buf_send[0] = 0x7e; //Flag = 0x7e
    buf_send[1] = 0x01; //Adress = 0x01
    buf_send[2] = 0x07; //Control = 0x07
    buf_send[3] = buf_send[1] ^ buf_send[2]; //BCC1 = Adress XOR Control
    buf_send[4] = 0x7e; //Flag = 0x7e

    write(fd, buf_send, 5);

    return 0;
}

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
}

int ll_open_Tx()
{

    int stage = 0;
    int flag;
    int adress;
    int control;
    alarmCount = 0;
    //alarm(0);
    send_SET();
    alarm(timeout);
    
    while(TRUE)
    {
        //Check timeouts
        if(alarmEnabled == FALSE && alarmCount < retries)
        {
            send_SET();
            alarm(timeout);
            alarmEnabled = TRUE;
        }
        else if(alarmEnabled == FALSE && alarmCount == retries)
        {
            return 1;
        }
        //Recieve message
        bytess = read(fd, buf_receive, 1);
        buf_receive[bytess] = '\0';
        //Process message
        switch (stage)
        {
            case START:
                if(buf_receive[0] == 0x7e)
                {
                    flag = buf_receive[0];
                    stage = FLAG;
                }
                break;
            case FLAG:
                if(buf_receive[0] == 0x01)
                {
                    stage = ADRESS;
                    adress = buf_receive[0];
                }
                else if(buf_receive[0] == 0x7e);
                else
                    stage = START;
                break;
            case ADRESS:
                if(buf_receive[0] == 0x07)
                {
                    stage = CONTROL;
                    control = buf_receive[0];
                }
                else if(buf_receive[0] == 0x7e)
                    stage = FLAG;
                else
                    stage = START;
                break;
            case CONTROL:
                if(buf_receive[0] == (adress ^ control))
                    stage = BCC;
                else if(buf_receive[0] == 0x7e)
                    stage = FLAG;
                else
                    stage = START;
                break;
            case BCC:
                if(buf_receive[0] == 0x7e)
                {
                    return 0;
                }
                else
                    stage = START;
                break;
            default:
                break;
        }
    }
}

int ll_open_Rx()
{
    int stage = START;
    int flag;
    int adress;
    int control;
    alarmCount = 0;
    
    while(stage != END)
    {
        //Recieve message
        bytess = read(fd, buf_receive, 1);
        buf_receive[bytess] = '\0';
        //Process message
        switch (stage)
        {
            case START:
                if(buf_receive[0] == 0x7e)
                {
                    flag = buf_receive[0];
                    stage++;
                }
                break;
            case FLAG:
                if(buf_receive[0] == 0x03)
                {
                    stage++;
                    adress = buf_receive[0];
                }
                else if(buf_receive[0] == 0x7e);
                else
                    stage = 0;
                break;
            case ADRESS:
                if(buf_receive[0] == 0x03)
                {
                    stage++;
                    control = buf_receive[0];
                }
                else if(buf_receive[0] == 0x7e)
                    stage = 1;
                else
                    stage = 0;
                break;
            case CONTROL:
                if(buf_receive[0] == (adress ^ control))
                    stage++;
                else if(buf_receive[0] == 0x7e)
                    stage = 1;
                else
                    stage = 0;
                break;
            case BCC:
                if(buf_receive[0] == 0x7e)
                    stage = END;
                else
                    stage = 0;
                break;
            default:
                break;
        }
    }

    return send_UA();
}

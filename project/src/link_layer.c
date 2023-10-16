// Link layer protocol implementation

#include "link_layer.h"
#include <fcntl.h>
#include <termios.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256

int fd;
struct termios oldtio;
struct termios newtio;
int alarmEnabled = FALSE;
int alarmCount = 0;
unsigned char buf_send[BUF_SIZE] = {0}; 
unsigned char buf_receive[2] = {0}; // +1: Save space for the final '\0' char
int bytess;

int setup(LinkLayer connectionParameters);

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    fd = setup(connectionParameters);

    if(fd < 0) return -1;

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

int setup(LinkLayer connectionParameters)
{
    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0){perror(connectionParameters.serialPort); return -1;}
    if(tcgetattr(fd, &oldtio) == -1){perror("tcgetattr"); return -1;}

    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0.1;
    newtio.c_cc[VMIN] = 1;
    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1){perror("tcsetattr"); return -1;}

    printf("New termios structure set\n");
    
    return fd;
}

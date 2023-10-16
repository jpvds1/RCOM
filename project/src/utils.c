#include "utils.h"

int setup(const char* serialPort)
{
    int fd = open(serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0){perror(serialPortName); exit(-1);}
    if(tcgetattr(fd, &oldtio) == -1){perror("tcgetattr"); exit(-1);}

    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0.1;
    newtio.c_cc[VMIN] = 1;
    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1){perror("tcsetattr"); exit(-1);}

    printf("New termios structure set\n");
}
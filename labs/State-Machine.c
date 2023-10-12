#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

#define FALSE 0
#define TRUE 1
#define BUF_SIZE 256
#define BAUDRATE B38400
#define _POSIX_SOURCE 1

int fd;
struct termios oldtio;
struct termios newtio;
unsigned char buf_send[BUF_SIZE] = {0};
unsigned char buf_receive[2] = {0}; // +1: Save space for the final '\0' char
int bytess;
char *serialPortName;

void setup()
{
    fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0){perror(serialPortName); exit(-1);}
    if(tcgetattr(fd, &oldtio) == -1){perror("tcgetattr"); exit(-1);}

    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0.1; // Inter-character timer unused
    newtio.c_cc[VMIN] = 1;  // Blocking read until 5 chars received
    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1){perror("tcsetattr"); exit(-1);}

    printf("New termios structure set\n");
}

void send_message()
{
    int flag = 0x7E;
    int adress = 0x03;
    int control = 0x03;
    int bcc = adress ^ control;
    
    memset(buf_send, BUF_SIZE, 0);  
    
    buf_send[1] = adress;
    buf_send[0] = flag;
    buf_send[2] = control;
    buf_send[3] = bcc;
    buf_send[4] = flag;
    
    int bytes = write(fd, buf_send, BUF_SIZE);
    //printf("%d bytes written\n", bytes);
}

void restore()
{
    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }
}

int recieve_message()
{
    bytess = read(fd, buf_receive, 1);
    buf_receive[bytess] = '\0';

    if (buf_receive[0] == 0x7e && buf_receive[4] == 0x7E && (buf_receive[1] ^ buf_receive[2] == buf_receive[3]))
    {
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    serialPortName = argv[1];

    setup();

    int stage = 0;

    int flag;
    int adress;
    int control;
    int STOP = FALSE;

    while (STOP != TRUE) //0x7e 0x03 0x03 a^b 0x7e
    {
        bytess = read(fd, buf_receive, 1);
        buf_receive[bytess] = '\0';
        switch (stage)
        {
        case 0:
            printf("stage 0\n");
            if(buf_receive[0] == 0x7e)
            {
                flag = buf_receive[0];
                stage++;
            }
            break;
        case 1:
            printf("stage 1\n");
            if(buf_receive[0] == 0x03)
            {
                stage++;
                adress = buf_receive[0];
            }
            else if(buf_receive[0] == 0x7e)
                break;
            else
                stage = 0;
            break;
        case 2:
            printf("stage 2\n");
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
        case 3:
            printf("stage 3\n");
            if(buf_receive[0] == adress ^ control)
                stage++;
            else if(buf_receive[0] = 0x7e)
                stage = 1;
            else
                stage = 0;
            break;
        case 4:
            printf("stage 4\n");
            if(buf_receive[0] == 0x7e)
                STOP = TRUE;
            else
                stage = 0;
            break;
        default:
            break;
        }
    }

    printf("\nflag = %d\n", flag);
    printf("adress = %d\n", adress);
    printf("control = %d\n", control);

    restore();

    close(fd);

    printf("Ending program\n");

    return 0;
}
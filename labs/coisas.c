// Write to serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256

volatile int STOP = FALSE;

int fd;
struct termios oldtio;
struct termios newtio;
unsigned char buf_send[BUF_SIZE] = {0};
unsigned char buf_receive[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char
char *serialPortName;

void setup()
{
    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    printf("New termios structure set\n");

    // Create string to send
    //memset(buf_send, BUF_SIZE, 0);
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
    printf("%d bytes written\n", bytes);
}

void receive_message()
{
    while (STOP == FALSE)
    {
        // Returns after 5 chars have been input
        int bytes = read(fd, buf_receive, BUF_SIZE);
        buf_receive[bytes] = '\0'; // Set end of string to '\0', so we can printf

        printf(":%s:%d\n", buf_receive, bytes);
        if (buf_receive[2] == 0x07)
        {
            if(buf_receive[1] ^ buf_receive[2] != buf_receive[3])
            {
                printf("erro D:");
            }
            STOP = TRUE;
            printf("it read\n");
            for(int i=0; i<5; i++)
            {
                printf("var = 0x%02X\n", buf_receive[i]);
            }
        }
    }
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

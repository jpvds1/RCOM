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
#include <stdbool.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256

//States (state machine)
enum States
{
    START,
    FLAG,
    ADRESS,
    CONTROL,
    BCC,
    END,
    PENDING,
    ACCEPTED,
    REJECTED,
    DESTUFF
};

//Connection variables
int fd;
struct termios oldtio;
struct termios newtio;
int retries;
int timeout;
bool one = FALSE;
bool ttx;
//Message buffer variables
unsigned char buf_send[MAX_PAYLOAD_SIZE * 2 + 6] = {0}; 
unsigned char buf_receive[2] = {0};
//Alarm variables
int alarmEnabled = TRUE;
int alarmCount = 0;
//Read variables
int bytess;
int STOP = FALSE;


//Setup Functions
int setup(LinkLayer connectionParameters);
void alarmHandler(int signal);
//Message related functions
int send_SU(char adress, char ctrl);
int send_inf_frame(bool tx, const unsigned char* buf, int bufSize);
//Read functionas
int read_control_frame();
int read_SU_frame(char adress, char ctrl);
//Aux functions
int ll_open_Tx();
int ll_open_Rx();
int ll_close_Tx();
int ll_close_Rx();
int stuffing(const unsigned char* buf, int size, unsigned char* newBuf, char bcc2);

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    fd = setup(connectionParameters);

    if(fd < 0) return -1;

    if(connectionParameters.role == LlTx)
    {
        if(ll_open_Tx()) return -1;
    }
    else
    {
        if(ll_open_Rx()) return -1;
    }

    return fd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    //Iniciar variaveis
    alarm(0);
    int result = PENDING; //resultado da leitura
    alarmCount = 0;       //Pending = Não recebeu mensagem antes do alarme acionar
                          //Rejected = Recebeu a mensagem de REJ
                          //Accepted = Recebeu a mensagem de RR  

    //loop para enviar inf frame e esperar pela mensagem
    while(alarmCount < retries && result != ACCEPTED)
    {
        send_inf_frame(1, buf, bufSize);
        alarm(timeout);
        alarmEnabled = TRUE;
        result = read_control_frame(); //State machine
        if(result == REJECTED){alarmCount = 0;} //Se for rejected da reset as tries
    }
    alarm(0);

    one = !one; //Mudar o numero da proxima frame

    if(result != ACCEPTED){return -1;}
    return bufSize;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    unsigned char* receive_bytes = (unsigned char*) malloc(MAX_PAYLOAD_SIZE * 2);
    int stage = START;
    int adress;
    int control;
    alarmCount = 0;
    int i = 0;

    while(stage != END)
    {
        bytess = read(fd, buf_receive, 1);
        buf_receive[bytess] = '\0';
        switch (stage)
        {
        case START:
            if(buf_receive[0] == 0x7e)
            {
                stage = FLAG;
            }
            break;

        case FLAG:
            if(buf_receive[0] == 0x03)
            {
                adress = buf_receive[0];
                stage = ADRESS;
            }
            else if(buf_receive[0] == 0x7e);
            else stage = START;
            break;

        case ADRESS:
            if(buf_receive[0] == 0x00)
            {
                control = buf_receive[0];
                one = FALSE;
                stage = CONTROL;
            }
            else if(buf_receive[0] == 0x40)
            {
                control = buf_receive[0];
                one = TRUE;
                stage = CONTROL;
            }
            else stage = START;
            break;

        case CONTROL:
            if(buf_receive[0] == (adress ^ control))
            {
                stage = BCC;
            }
            else stage = START;
            break;

        case BCC:
            if(buf_receive[0] == 0x7d)
            {
                stage = DESTUFF;
            }
            else if(buf_receive[0] == 0x7e)
            {
                stage = END;
            }
            else
            {
                receive_bytes[i] = buf_receive[0];
                i++;
            }
            break;

        case DESTUFF:
            if(buf_receive[0] == 0x5e)
            {
                receive_bytes[i] = 0x7e;
                i++;
            }
            else if(buf_receive[0] == 0x5d)
            {
                receive_bytes[i] = 0x7d;
                i++;
            }
            stage = BCC;
            break;

        default:
            break;
        }
    }
    i--;

    unsigned char bcc2 = receive_bytes[0];
    for(int j = 1; j < i; j++)
    {
        bcc2 ^= receive_bytes[j];
    }
    if(bcc2 != receive_bytes[i])
    {
        if(one == TRUE) send_SU(0x01, 0X81);
        else send_SU(0x01, 0x01);
    }
    else
    {
        if(one == TRUE) send_SU(0x01, 0X85);
        else send_SU(0x01, 0X05);
    }

    memcpy(packet, receive_bytes, i);
    free(receive_bytes);

    i--;

    return i;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    if(ttx == TRUE)
    {
        return ll_close_Tx();
    }
    else
    {
        return ll_close_Rx();
    }
}

////////////////////////////////////////////////
// SETUP FUNCTIONS
////////////////////////////////////////////////
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
        ttx = FALSE;
        newtio.c_cc[VTIME] = 0;
        newtio.c_cc[VMIN] = 0;
    }
    else
    {
        ttx = TRUE;
        newtio.c_cc[VTIME] = 0;
        newtio.c_cc[VMIN] = 0;
    }
    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1){perror("tcsetattr"); return 1;}
    
    return fd;
}

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
}

////////////////////////////////////////////////
// MESSAGE FUNCTIONS
////////////////////////////////////////////////
int send_SU( char adress, char ctrl) //Send UA message from Rx to Tx
{
    memset(&buf_send, 0, BUF_SIZE); //Clear sending buffer

    buf_send[0] = 0x7e; //Flag = 0x7e
    buf_send[1] = adress; //Adress = 0x01
    buf_send[2] = ctrl; //Control = 0x07
    buf_send[3] = buf_send[1] ^ buf_send[2]; //BCC1 = Adress XOR Control
    buf_send[4] = 0x7e; //Flag = 0x7e

    if(write(fd, buf_send, 5) < 0){perror("send_SU write failed\n"); return 1;}

    return 0;
}

int send_inf_frame(bool tx, const unsigned char* buf, int bufSize)
{
    //Iniciar variaveis
    int control;
    int adress;
    int bcc2 = buf[0];
    unsigned char* stuffedBuf = (unsigned char*) malloc(bufSize);
    int finalSize;

    //Set sender values
    if(tx == 1){adress = 0x03;}
    else {adress = 0x01;}
    if(one == 1){control = 0x40;}
    else {control = 0x00;}

    buf_send[0] = 0x7E;
    buf_send[1] = adress;
    buf_send[2] = control;
    buf_send[3] = adress ^ control;

    for(int i = 1; i < bufSize; i++) //Fazer o ^ a todos os bytes dos dados
    {
        bcc2 = bcc2 ^ buf[i];
    }

    finalSize = stuffing(buf, bufSize, stuffedBuf, bcc2);
    if(memcpy(buf_send + 4, stuffedBuf, finalSize) == NULL){free(stuffedBuf); return 1;} //Copiar a data para a frame
    //free(stuffedBuf);

    buf_send[finalSize+4] = 0x7e;

    if(write(fd, buf_send, finalSize+5) < 0){perror("send_Set write failed\n"); return 1;}

    return 0;
}

////////////////////////////////////////////////
// READ FUNCTIONS
////////////////////////////////////////////////
int read_control_frame()
{
    int stage = START;
    int adress;
    int control;
    int value = PENDING;

    while(alarmEnabled == TRUE && stage != END)
    {
        bytess = read(fd, buf_receive, 1);
        buf_receive[bytess] = '\0';
        switch (stage)
        {
        case START:
                if(buf_receive[0] == 0x7e)
                {
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
                    stage = 0;
                break;

            case ADRESS:
                if(buf_receive[0] == 0x85 && one == 1)
                {
                    stage = CONTROL;
                    control = buf_receive[0];
                    value = ACCEPTED;
                }
                else if(buf_receive[0] == 0x05 && one == 0)
                {
                    stage = CONTROL;
                    control = buf_receive[0];
                    value = ACCEPTED;
                }
                else if(buf_receive[0] == 0x01 && one == 0)
                {
                    stage = CONTROL;
                    control = buf_receive[0];
                    value = REJECTED;
                }
                else if(buf_receive[0] == 0x81 && one == 1)
                {
                    stage = CONTROL;
                    control = buf_receive[0];
                    value = REJECTED;
                }
                else if(buf_receive[0] == 0x7e)
                    stage = FLAG;
                else
                {
                    stage = START;
                }
                break;

            case CONTROL:
                if(buf_receive[0] == (adress ^ control))
                {
                    stage = BCC;
                }
                else if(buf_receive[0] == 0x7e)
                    stage = FLAG;
                else
                    stage = START;
                break;

            case BCC:
                if(buf_receive[0] == 0x7e)
                {
                    stage = END;
                }
                else
                    stage = START;
                break;

            default:
                break;
        }
    }

    if(stage != END){value = PENDING;}
    return value;
}

int read_SU_frame(char adress, char ctrl)
{
    int stage = START;
    int adr_r;
    int ctrl_r;
    int value = PENDING;

    while(alarmEnabled == TRUE && stage != END)
    {
        bytess = read(fd, buf_receive, 1);
        buf_receive[bytess] = '\0';
        switch (stage)
        {
        case START:
                if(buf_receive[0] == 0x7e)
                {
                    stage = FLAG;
                }
                break;

            case FLAG:
                if(buf_receive[0] == adress)
                {
                    stage = ADRESS;
                    adr_r = buf_receive[0];
                }
                else if(buf_receive[0] == 0x7e);
                else
                    stage = 0;
                break;

            case ADRESS:
                if(buf_receive[0] == ctrl)
                {
                    stage = CONTROL;
                    ctrl_r = buf_receive[0];
                }
                else if(buf_receive[0] == 0x7e)
                    stage = FLAG;
                else
                {
                    stage = START;
                }
                break;

            case CONTROL:
                if(buf_receive[0] == (adr_r ^ ctrl_r))
                {
                    stage = BCC;
                }
                else if(buf_receive[0] == 0x7e)
                    stage = FLAG;
                else
                    stage = START;
                break;

            case BCC:
                if(buf_receive[0] == 0x7e)
                {
                    stage = END;
                    value = ACCEPTED;
                }
                else
                    stage = START;
                break;

            default:
                break;
        }
    }

    if(stage != END){value = PENDING;}
    return value;
}

////////////////////////////////////////////////
// AUX FUNCTIONS
////////////////////////////////////////////////
int ll_open_Tx()
{
    alarmCount = 0;
    int result = PENDING;
    alarm(0);
    
    while(alarmCount < retries && result != ACCEPTED)
    {
        send_SU(0x03, 0x03);
        alarm(timeout);
        alarmEnabled = TRUE;
        result = read_SU_frame(0x01, 0x07);
    }

    if(result == ACCEPTED) return 0;
    return -1;
}

int ll_open_Rx()
{
    alarmCount = 0;
    
    read_SU_frame(0x03, 0x03);

    return send_SU(0x01, 0X07);
}

int ll_close_Tx()
{
    //Iniciar variaveis
    alarm(0);
    int result = PENDING; //resultado da leitura
    alarmCount = 0;       //Pending = Não recebeu mensagem antes do alarme acionar
                          //Rejected = Recebeu a mensagem de REJ
                          //Accepted = Recebeu a mensagem de RR  

    //loop para enviar DISC frame e esperar pela mensagem
    while(alarmCount < retries && result != ACCEPTED)
    {
        send_SU(0x03, 0x0b);
        alarm(timeout);
        alarmEnabled = TRUE;
        result = read_SU_frame(0x01, 0x0B); //State machine
    }
    alarm(0);

    if(result != ACCEPTED){return -1;}
    send_SU(0x03, 0x07);
    return 0;
}

int ll_close_Rx()
{
    read_SU_frame(0x03, 0x0b);

    alarm(0);
    int result = PENDING;
    alarmCount = 0;

    while(alarmCount < retries && result != ACCEPTED)
    {
        send_SU(0x01, 0x0b);
        alarm(timeout);
        alarmEnabled = TRUE;
        result = read_SU_frame(0x03, 0x07);
    }

    if(result != ACCEPTED) return -1;
    return 0;
}

int stuffing(const unsigned char* buf, int size, unsigned char* newBuf, char bcc2)
{
    int extra = 0;

    for(int i = 0; i < size; i++)
    {
        if(buf[i] == 0x7e)
        {
            newBuf[i + extra] = 0x7d;
            extra++;
            newBuf = realloc(newBuf, size + extra);
            newBuf[i + extra] =  0x5e;
        }
        else if(buf[i] == 0x7d)
        {
            newBuf[i + extra] = 0x7d;
            extra++;
            newBuf = realloc(newBuf, size + extra);
            newBuf[i + extra] =  0x5d;
        }
        else
        {
            newBuf[i + extra] = buf[i];
        }
    }

    if(bcc2 == 0x7d)
    {
        extra += 2;
        newBuf = realloc(newBuf, size + extra);
        newBuf[size + extra - 2] = 0x7d;
        newBuf[size + extra - 1] = 0x5d;
    }
    else if(bcc2 == 0x7e)
    {
        extra += 2;
        newBuf = realloc(newBuf, size + extra);
        newBuf[size + extra - 2] = 0x7d;
        newBuf[size + extra - 1] = 0x5e;
    }
    else
    {
        extra++;
        newBuf = realloc(newBuf, size + extra);
        newBuf[size + extra - 1] = bcc2;
    }

    return size + extra;
}

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
#define _POSIX_SOURCE 1
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
    DESTUFF,
    DISCARD,
    OTHER
};

//Connection variables
int fd;
struct termios oldtio;
struct termios newtio;
int retries;
int timeout;
bool frame_number = FALSE;
bool transmitter;
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
int restore();
//Message related functions
int send_SU(char adress, char ctrl);
int send_inf_frame(bool tx, const unsigned char* buf, int bufSize);
//Read functionas
int read_control_frame();
int read_SU_frame(char adress, char ctrl);
int read_DISC_frame();
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

    if(fd < 0){perror("fd < 0"); exit(-1);}

    //Call the relevant aux functions depending on the role
    if(connectionParameters.role == LlTx)
    {
        if(ll_open_Tx()){perror("ll_open_tx fail"); exit(-1);}
    }
    else
    {
        if(ll_open_Rx()){perror("ll_open_rx fail"); exit(-1);}
    }

    return fd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    //Reset alarm and set message result to pending
    alarm(0);
    int result = PENDING; //result of read funtions
    alarmCount = 0;       //Pending = Still awaiting for a message
                          //Rejected = Got a REJ message
                          //Accepted = Got any type of confirmation message (ex: RR)

    //Sends an information frame until a confirmation message is recieved or the limit of time outs is exceded
    while(alarmCount < retries && result != ACCEPTED)
    {
        send_inf_frame(1, buf, bufSize);
        alarm(timeout);
        alarmEnabled = TRUE;
        result = read_control_frame(); //State machine
        if(result == REJECTED){alarmCount = 0;} //If a rejection message is recieved, the number of time outs is reseted
    }
    alarm(0);

    frame_number = !frame_number; //Change the frame_number value for the frame to come

    if(result != ACCEPTED){return -1;}
    return bufSize;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    unsigned char* received_bytes = (unsigned char*) malloc(MAX_PAYLOAD_SIZE * 2);
    int stage = START;
    int adress;
    int control;
    alarmCount = 0;
    int i = 0;
    bool wrong_bcc = FALSE;
    bool SET = FALSE;
    bool duplicate = FALSE;
    //sleep(1);
    //sleep(2);
    //sleep(3);

    while(stage != END)
    {
        //Read byte
        bytess = read(fd, buf_receive, 1);
        buf_receive[bytess] = '\0';
        if(bytess > 0)
        {
            switch (stage)
            {
            case START:
                if(buf_receive[0] == 0x7e)
                {
                    if(SET == TRUE)
                    {
                        stage = END;
                    }
                    else
                    {
                        stage = FLAG;
                    }
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
                if(buf_receive[0] == 0x03)
                {
                    SET = TRUE;
                    stage = CONTROL;
                }
                else if(buf_receive[0] == 0x00)
                {
                    control = buf_receive[0];
                    if(frame_number == TRUE)
                    {
                        duplicate = TRUE;
                        stage = DISCARD;
                    }
                    else
                    {
                        stage = CONTROL;
                    }
                }
                else if(buf_receive[0] == 0x40)
                {
                    control = buf_receive[0];
                    if(frame_number == FALSE)
                    {
                        duplicate = TRUE;
                        stage = DISCARD;
                    }
                    else
                    {
                        stage = CONTROL;
                    }
                }
                else stage = START;
                break;

            case CONTROL:
                if(SET == TRUE && buf_receive[0] == (adress ^ control))
                {   
                    stage = START;
                }
                else if(buf_receive[0] == (adress ^ control))
                {
                    stage = BCC;
                }
                else
                {
                    stage = DISCARD;
                    wrong_bcc = TRUE;
                }
                break;

            case BCC:
                if(buf_receive[0] == 0x7d) //If the destuff flag appears, treat it in the next loop
                {
                    stage = DESTUFF;
                }
                else if(buf_receive[0] == 0x7e) //0x7e is the end flag
                {
                    stage = END;
                }
                else
                {
                    received_bytes[i] = buf_receive[0];
                    i++;
                }
                break;

            case DESTUFF:
                if(buf_receive[0] == 0x5e) //Destuffing
                {
                    received_bytes[i] = 0x7e;
                    i++;
                }
                else if(buf_receive[0] == 0x5d)
                {
                    received_bytes[i] = 0x7d;
                    i++;
                }
                stage = BCC;
                break;

            case DISCARD:
                if(buf_receive[0] == 0x7e)
                {
                    stage = END;
                }
                break;

            default:
                break;
            }
        }
    }
    //check if the bcc2 is correct and send the respective message
    if(wrong_bcc == TRUE)
    {
        return -1;
    }
    if(duplicate == TRUE)
    {
        if(frame_number == TRUE) {send_SU(0x01, 0X85);}
        else {send_SU(0x01, 0X05);}
        return -1;
    }
    //i points to bcc2, by doing i-- it now points to the last byte of data
    i--;

    //calculate the bcc2
    unsigned char bcc2 = received_bytes[0];
    for(int j = 1; j < i; j++)
    {
        bcc2 ^= received_bytes[j];
    }

    if(bcc2 != received_bytes[i])
    {
        if(frame_number == TRUE) {send_SU(0x01, 0X01);}
        else {send_SU(0x01, 0x81);}
        return -1;
    }
    else
    {
        frame_number = !frame_number;
        if(frame_number == TRUE) {send_SU(0x01, 0X85);}
        else {send_SU(0x01, 0X05);}
    }

    //copy the data bytes to the packet
    if(memcpy(packet, received_bytes, i) == NULL){perror("llread memcpy fail"); exit(-1);}
    free(received_bytes);

    return i;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    //Call the relevant aux functions depending on the role
    if(transmitter == TRUE)
    {
        return ll_close_Tx();
    }
    else
    {
        return ll_close_Rx();
    }

    return restore();
}

////////////////////////////////////////////////
// SETUP FUNCTIONS
////////////////////////////////////////////////
int setup(LinkLayer connectionParameters) //Setup the connection
{
    (void)signal(SIGALRM, alarmHandler);

    retries = connectionParameters.nRetransmissions;
    timeout = connectionParameters.timeout;

    //Setup srand for testing purposes
    //srand(time(0));

    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0){perror(connectionParameters.serialPort); exit(-1);}
    if(tcgetattr(fd, &oldtio) == -1){perror("tcgetattr"); exit(-1);}

    memset(&newtio, 0, sizeof(newtio));
    newtio.c_cflag = connectionParameters.baudRate | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    if(connectionParameters.role == LlRx)
    {
        transmitter = FALSE;
        newtio.c_cc[VTIME] = 0;
        newtio.c_cc[VMIN] = 0;
    }
    else
    {
        transmitter = TRUE;
        newtio.c_cc[VTIME] = 0;
        newtio.c_cc[VMIN] = 0;
    }
    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1){perror("tcsetattr"); exit(-1);}
    
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
    //int r = rand() % 100;
    memset(&buf_send, 0, BUF_SIZE); //Clear sending buffer

    buf_send[0] = 0x7e; //Flag = 0x7e
    buf_send[1] = adress; //Adress = 0x03 || 0x01
    buf_send[2] = ctrl; //Control = 0x03 || 0x07 || 0x05 || 0x85 || 0x01 || 0x81 || 0x0B
    buf_send[3] = buf_send[1] ^ buf_send[2]; //BCC1 = Adress XOR Control
    buf_send[4] = 0x7e; //Flag = 0x7e

    /*
    if(r < 10)
    {
        printf("wrong bcc on SU frame\n");
        buf_send[3] = ~buf_send[3];
    }
    */

    if(write(fd, buf_send, 5) < 0){perror("send_SU write failed\n"); exit(-1);}

    return 0;
}

int send_inf_frame(bool tx, const unsigned char* buf, int bufSize)
{
    //int r = rand() % 100;
    //set vatiables
    int control;
    int adress;
    int bcc2 = buf[0];
    unsigned char* stuffedBuf = (unsigned char*) malloc(bufSize);
    int finalSize;

    //Set sender values
    if(tx == 1){adress = 0x03;}
    else {adress = 0x01;}
    if(frame_number == 1){control = 0x40;}
    else {control = 0x00;}

    //Set buf_send
    buf_send[0] = 0x7E;
    buf_send[1] = adress;
    buf_send[2] = control;
    buf_send[3] = adress ^ control;

    //Calculate bcc2
    for(int i = 1; i < bufSize; i++)
    {
        bcc2 = bcc2 ^ buf[i];
    }
    /*
    if(r < 10)
    {
        printf("inf frame bcc2 wrong\n");
        bcc2 = ~bcc2;
    }
    */

    //Stuff the data
    finalSize = stuffing(buf, bufSize, stuffedBuf, bcc2);
    if(memcpy(buf_send + 4, stuffedBuf, finalSize) == NULL){perror("send_inf_frame memcpy fail"); free(stuffedBuf); exit(-1);}
    //free(stuffedBuf);

    //Add the final flag
    buf_send[finalSize+4] = 0x7e;

    if(write(fd, buf_send, finalSize+5) < 0){perror("send_Set write failed\n"); exit(-1);}

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
        if(bytess > 0)
        {
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
                    if(buf_receive[0] == 0x85 && frame_number == 0)
                    {
                        stage = CONTROL;
                        control = buf_receive[0];
                        value = ACCEPTED;
                    }
                    else if(buf_receive[0] == 0x05 && frame_number == 1)
                    {
                        stage = CONTROL;
                        control = buf_receive[0];
                        value = ACCEPTED;
                    }
                    else if(buf_receive[0] == 0x01 && frame_number == 1)
                    {
                        stage = CONTROL;
                        control = buf_receive[0];
                        value = REJECTED;
                    }
                    else if(buf_receive[0] == 0x81 && frame_number == 0)
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
        if(bytess > 0)
        {
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
    }

    if(stage != END){value = PENDING;}
    return value;
}

int read_DISC_frame()
{
    int stage = START;
    int adress;
    int control;
    int value = PENDING;

    while(alarmEnabled == TRUE && stage != END)
    {
        bytess = read(fd, buf_receive, 1);
        buf_receive[bytess] = '\0';
        if(bytess > 0)
        {
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
                        stage = ADRESS;
                        adress = buf_receive[0];
                    }
                    else if(buf_receive[0] == 0x7e);
                    else
                        stage = START;
                    break;

                case ADRESS:
                    if(buf_receive[0] == 0x0b)
                    {
                        stage = CONTROL;
                        control = buf_receive[0];
                    }
                    else if(buf_receive[0] == 0x00 || buf_receive[0] == 0x40)
                    {
                        stage = DISCARD;
                        value = OTHER;
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
                        value = ACCEPTED;
                    }
                    else
                        stage = START;
                    break;

                case DISCARD:
                    if(buf_receive[0] == 0x7e)
                    {
                        stage = END;
                    }
                    break;

                default:
                    break;
            }
        }
    }

    if(stage != END){value = PENDING;}
    if(value == OTHER)
    {
        if(frame_number == TRUE) {send_SU(0x01, 0X05);}
        else {send_SU(0x01, 0X85);}
    }
    return value;
}

////////////////////////////////////////////////
// AUX FUNCTIONS
////////////////////////////////////////////////
int ll_open_Tx()
{
    //restart the alarm variables and set to pending response
    alarmCount = 0;
    int result = PENDING;
    alarm(0);
    
    //sends an initial message, then waits until either recieves a message or times out
    while(alarmCount < retries && result != ACCEPTED)
    {
        send_SU(0x03, 0x03);
        alarm(timeout);
        alarmEnabled = TRUE;
        result = read_SU_frame(0x01, 0x07);
    }
    alarm(0);

    if(result == ACCEPTED) return 0;
    return -1;
}

int ll_open_Rx()
{    
    //waits indefinitely to read the SET message
    read_SU_frame(0x03, 0x03);

    //sends the UA message back
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
    alarm(0);
    int result = PENDING;
    alarmCount = 0;

    while (result != ACCEPTED)
    {
        result = read_DISC_frame();
    }
    
    result = PENDING;

    while(alarmCount < retries && result != ACCEPTED)
    {
        send_SU(0x01, 0x0b);
        alarm(timeout);
        alarmEnabled = TRUE;
        result = read_SU_frame(0x03, 0x07);
    }
    alarm(0);

    if(result != ACCEPTED) return -1;
    return 0;
}

int stuffing(const unsigned char* buf, int size, unsigned char* newBuf, char bcc2)
{
    int extra = 0;

    //Iterate through every byte to check for stuffing needs
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

    //Stuff the bcc2 if necessary
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

int restore()
{
    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}


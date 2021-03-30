#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/* === Define degli OPCODE === */

#define RRQ 1
#define DATA 3
#define ACK 4
#define ERROR 5

/* === PACKET_CREATE: funzione di utilità che crea i pacchetti da inviare al client === */

/* La funzione crea sia pacchetti DATA che ERROR, quindi alcuni argomenti saranno inutili in uno dei due casi */
int packet_create(int type ,char* buffer, int block, FILE *fp, char* mode, int error_num, char* errorMsg)
{
    /* Inizializzazione delle variabili e gestione della Endianness
    opcode_error = variabile con il valore dell'OPCODE di tipo ERROR, scritto Big Endian
    opcode_data = variabile con il valore dell'OPCODE di tipo DATA, scritto Big Endian
    block_number = numero di blocco da inserire nel pacchetto di tipo DATA
    error_code = codice di errore da inserire nel pacchetto di tipo ERROR (vedere RFC 1350 per i codici)
    length = indica la lunghezza del pacchetto
    i = variabile di scorrimento da usare nel for
    */

    uint16_t    opcode_error = htons(ERROR), 
                opcode_data = htons(DATA),        
                block_number = htons(block),      
                error_code = htons(error_num);   
                 
    int length = 0;
    int i = 0;

    // Cleaning time
    memset(buffer, 0, 516);

    if(type == ERROR)
    {
        /*
        2 bytes  2 bytes        string    1 byte
        ----------------------------------------
         | 05    |  ErrorCode |   ErrMsg   |  0 |
        ----------------------------------------
        */

        memcpy(buffer, &opcode_error, 2);
        memcpy(&buffer[2], &error_code, 2);
        strcpy(&buffer[4], errorMsg);
        length = 5 + strlen(errorMsg) + 1;
    }

    if(type == DATA)
    {
        /*
        2 bytes    2 bytes       n bytes
        ---------------------------------
        | 03    |   Block #  |    Data   |
        ---------------------------------
        */

        memcpy(buffer, &opcode_data, 2);
        memcpy(&buffer[2], &block_number, 2);

        /* Gestione del diverso modo di scrittura per .txt e .bin */
        if(strcmp(mode, "netascii") == 0)
        {
            for(i = 0; i < 512; ++i)
            {
                buffer[4+i] = fgetc(fp);    // fgetc prende un carattere dal file fp
                if(feof(fp))                // feof ritorna 1 se siamo alla fine del file,
                    break;
            }
        }
        else if(strcmp(mode, "octet") == 0)
        {
            i = fread(&buffer[4], 1, 512, fp);  //fread legge dal file fp di tipo .bin 1 byte 512 volte
        }
        /* La i indica i caratteri scritti per .txt o il numero di byte scritti per .bin,
        ci sommo 4 (opcode e block_number) per ottenere la lunghezza del pacchetto */
        length = i + 4; 
    }
    return length;
}

/* === PROCESS_REQUEST: funzione di utilità che gestisce le richieste in arrivo dal client === */
void process_request(char* buffer, int new_sd, struct sockaddr_in* cl_addr, char* directory)
{
    /* Inizializzazione delle variabili
    rcv_len = indice di scorrimento del pacchetto ricevuto
    ret = valore di ritorno di alcune funzioni
    len = lunghezza del pacchetto da inviare
    block_number = numero di blocco da inserire nei pacchetti di tipo DATA
    rcv_opcode = valore dell'OPCODE del pacchetto di richiesta ricevuto
    ack_opcode = valore dell'OPCODE del pacchetto che dovrebbe essere un ACK
    file_name = nome del file richiesto dal client (MAX 100 caratteri, come per il client)
    mode = modalità di trasferimento (testuale o binario)
    fp = puntatore al file
    file_path = percorso completo del file, da usare nella fopen 
                (sarà la concatenazione della directory e del file_name)
    cl_len = dimensioni della struttura cl_addr
    */

    int rcv_len, ret,len;
    uint16_t    block_number = 1,
                rcv_opcode,
                ack_opcode;
    char file_name[100];
    char mode[9];
    FILE* fp = NULL;
    char file_path[355];
    socklen_t cl_len;

    memcpy(&rcv_opcode, buffer, 2);
    rcv_opcode = ntohs(rcv_opcode);
    rcv_len = 2;
    cl_len = sizeof(*cl_addr);

    /* Il client può mandare solo RRQ e ACK, ma i pacchetti ACK li gestisco dopo l'invio dei pacchetti
    al client, quindi in questo momento l'unico OPCODE corretto è RRQ */
    if(rcv_opcode != RRQ)
    {
        printf("Il client ha richiesto un'operazione TFTP illegale, codice: %i \n", rcv_opcode);
        len = packet_create(ERROR, buffer, 0, fp, "", 4, "Illegal TFTP operation."); 
        ret = sendto(new_sd, (void*)buffer, len, 0, (struct sockaddr*)cl_addr, cl_len);
        if (ret < 0)
        {
            printf("Errore nella sendto. \n");
            exit(1);
        }
    }

    /* Qui gestisco la richiesta che ha come OPCODE RRQ */
    
    strcpy(file_name, &buffer[rcv_len]);    // recupero il file_name
    rcv_len += strlen(file_name) + 1;
    strcpy(mode, &buffer[rcv_len+1]);       // recupero la modalità
    rcv_len += strlen(mode) + 1;
         

    /* Fase di apertura del file richiesto */

    strcpy(file_path, directory);           // copio la prima parte del file_path
    strcat(file_path, file_name);           // concateno la seconda parte per ottenere il file_path completo

    if(strcmp(mode, "netascii") == 0)
        fp = fopen(file_path, "r");
    else
        fp = fopen(file_path, "rb");

    if(fp == NULL)
    {
        printf("Il client ha richiesto un file non presente. \n");
        len = packet_create(ERROR, buffer, 0, fp, "", 1, "File not found."); 
        ret = sendto(new_sd, (void*)buffer, len, 0, (struct sockaddr*)cl_addr, cl_len);
        exit(1);
    }

    do
    {
        /* Creazione e invio del pacchetto DATA */

        len = packet_create(DATA, buffer, block_number, fp, mode, 0, "");
        ret = sendto(new_sd, (void*)buffer, len, 0, (struct sockaddr*)cl_addr, cl_len);
        if(ret < 0)
        {
            printf("Errore nella sendto.\n");
            exit(1);
        }
	printf("Pacchetto DATA inviato al client, in attesa di ACK. \n");

        /* Ricezione dell'ACK */

        ret = recvfrom(new_sd, buffer, 4, 0, (struct sockaddr*)cl_addr, &cl_len);
        if(ret < 0)
        {
            printf("Errore nella ricezione dell'ACK. \n");
            exit(1);
        }
        memcpy(&ack_opcode, buffer, 2);
        ack_opcode = ntohs(ack_opcode);
        if(ack_opcode != ACK)
        {
            printf("Il pacchetto ricevuto non è un ACK. \n");
            exit(1);
        }
	printf("ACK ricevuto correttamente dal client. \n");

        block_number++;
    } while (len - 4 == 512);       // se len - 4 < 512 allora è l'ultimo blocco DATA da inviare
    printf("Ultimo blocco inviato, trasferimento completato. \n");
    fclose(fp);  
}

/* === MAIN === */

int main(int argc, char* argv[])
{
    int ret, 
        sd, 
        new_sd;
    socklen_t cl_len;
    uint16_t port;
    pid_t pid;
    struct sockaddr_in  my_addr, 
                        cl_addr, 
                        child_addr;
    char directory[255];
    char buffer[516];

    /* Il server deve essere avviato con la seguente sintassi:
    ./server.c <porta> <directory> */

    if(argc != 3){
        printf("Sintassi errata nell'avvio del client.\n");
        exit(1);
    }

    // Cleaning time
    memset(buffer, 0, 516);
    memset(directory, 0, 255);
    memset(&cl_addr, 0, sizeof(cl_addr)); 
    memset(&my_addr, 0, sizeof(my_addr));

    port = atoi(argv[1]);
    port = htons(port);         // Recupero la porta dall'argomento della chiamata del main

    strcpy(directory, argv[2]);         // Prendo la directory passata come argomento del main
	directory[strlen(argv[2])] = '\0';  // Manca il carattere di fine stringa, lo aggiungo manualmente

    cl_len = sizeof(cl_addr);

    /* Creazione socket */

    printf("Creazione socket in corso. \n");
    sd = socket(AF_INET, SOCK_DGRAM, 0);   
    if(!sd)
    {
        printf("Creazione socket fallita. \n");
        exit(1);
    }  
    printf("Creazione socket avvenuta. \n");


    /* Creazione indirizzo del socket */

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = port;
    my_addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr));
    if(ret < 0)
    {
        printf("Errore durante la funzione bind. \n");
        exit(1);
    }
    else
        printf("Bind effettuata correttamente, il server è in ascolto. \n");

    /* Inizio della fase di ascolto e di ricezione delle richieste */

    while(1)
    {
        ret = recvfrom(sd, buffer, 516, 0, (struct sockaddr*)&cl_addr, &cl_len);      
        if(ret < 0)
        {
            printf("Errore in fase di ricezione. \n");
            return 1;
        }
        else if(!ret)
        {
            printf("Il socket remoto si è chiuso. \n");
            return 1;
        }
	printf("Ricezione della richiesta avvenuta. \n");

        /* Fork di processo */

        pid = fork();

        if(pid == -1)
            {
                printf("Errore nella fork(). \n");
            }
        if(pid == 0)        // siamo nel figlio
        {
            close(sd); 

            /* Creazione socket */

            new_sd = socket(AF_INET, SOCK_DGRAM, 0);

            /* Creazione indirizzo del socket */

            memset(&child_addr, 0, sizeof(child_addr));
            child_addr.sin_family = AF_INET;
            child_addr.sin_port = htons(0);
            child_addr.sin_addr.s_addr = INADDR_ANY;

            ret = bind(new_sd, (struct sockaddr*)&child_addr, sizeof(child_addr));
            if(ret < 0)
            {
                printf("Errore nella bind.\n");
                exit(1);
            }
            process_request(buffer, new_sd, &cl_addr, directory);
            exit(0);
        }
    }                       // siamo nel padre
    close(sd);  
    return 0;
}

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

/* === PRINT_COMMANDS: funzione di utilità che mostra tutti i comandi all'avvio o per il comando !help === */

void print_commands(){
    printf("Sono disponibili i seguenti comandi:\n"
		    "!help --> mostra l'elenco dei comandi disponibili\n"
            "!mode {txt|bin} --> imposta il modo di trasferimento dei files (testo o binario)\n"
            "!get filename nome_locale --> richiede al server il file di nome <filename> e lo salva localmente con il nome <nome_locale>\n"
			"!quit --> termina il client\n\n");
}

/* === MODE_CONTROL: funzione di utilità che controlla e gestisce la modalità inserita dall'utente tramite !mode === */

const char* mode_control(char* inserted_mode)
{
    /* Inizializzazione delle variabili:
    work = variabile che riceve la modalità inserita dall'utente (è il ritorno della funzione)
    aus_command = è una variabile di scarto, mi serve solo da parametro per la sscanf
    */
    char* work;
    char aus_command[6];

    work = (char*)malloc(sizeof(char)*4);
    sscanf(inserted_mode, "%s %s", aus_command, work);

    if(strcmp(work, "txt") == 0)
        printf("Modo di trasferimento testuale configurato. \n");
    else if (strcmp(work, "bin") == 0)
        printf("Modo di trasferimento binario configurato. \n");
    else
    {
        printf("Modo di trasferimento errato, usare {txt|bin}. \n");
        strcpy(work, "err");
    }
    return work;
}

/* === PACKET_CREATE: funzione di utilità che crea i pacchetti da inviare al server === */

/* La funzione crea sia pacchetti RRQ che ACK, quindi alcuni argomenti saranno inutili in uno dei due casi */
int packet_create(int type, char* mode, char* filename, char* buffer, uint16_t block)
{
    /* Inizializzazione delle variabili
    opcode = OPCODE da inserire nel pacchetto
    packet_length = lunghezza del pacchetto (sarà la return della funzione)
    */

    uint16_t opcode;
    int packet_length = 0;


    // Cleaning time
    memset(buffer, 0, 516);

    if(type == RRQ)
    {
        /*

        2 bytes    string   1 byte     string   1 byte
        -----------------------------------------------
        |   01  |  Filename  |   0  |    Mode    |  0 |
        -----------------------------------------------

        */

        // 2 bytes di OPCODE
        opcode = htons(RRQ);
        memcpy(buffer, &opcode, 2);
        packet_length += 2;

        // Filename
        strcpy(&buffer[2], filename);
        packet_length += strlen(filename) + 1;

        // 1 byte a 0
        memset(&buffer[packet_length], 0, 1);
        packet_length ++;

        // Mode
        if(strcmp(mode, "txt") == 0)
        {
            strcpy(&buffer[packet_length], "netascii");
            packet_length += strlen("netascii") + 1;
        }
        else
        {
            strcpy(&buffer[packet_length], "octet");
            packet_length += strlen("octet") + 1;
        }

        // 1 byte a 0
        memset(&buffer[packet_length], 0, 1);
        packet_length ++;

        return packet_length;
    }
    else if(type == ACK)
    {
        /*
        2 bytes    2 bytes
        -------------------
        |  04  | Block #  |
        -------------------
        */

       // 2 bytes di OPCODE
        opcode = htons(ACK);
        memcpy(buffer, &opcode, 2);
        packet_length += 2;

        // 2 bytes di Block
        block = htons(block);
        memcpy(&buffer[2], &block, 2);
        packet_length += 2;
        
        return packet_length;
    }
    else
        return 0;
}

void get_file(int socket, char* mode, char* nome_locale)
{
    /* Inizializzazione delle variabili
    opcode = OPCODE del pacchetto ricevuto
    error_type = valore del codice di errore contenuto nel pacchetto di tipo ERROR
    block = indice del blocco che DOVREI ricevere in assenza di errori
    pktblock = indice del blocco ricevuto dal server
    get_buffer = ho bisogno di un buffer dove salvare il dato in arrivo dal server, anche qui la dimensione
                 massima di un pacchetto può essere 516
    ret = variabile di ritorno di varie funzioni
    retwrite = byte scritti nella modalità binaria
    i = indice del for
    src_len = dimensioni di src_addr
    fp = puntatore al file

    */
    struct sockaddr_in src_addr;
    uint16_t    opcode, 
                error_type,
                block = 0,
                pktblock = 0;
    char get_buffer[516];
    int ret, retwrite, i = 0, pkt_len;
    socklen_t src_len;
    FILE *fp;            

    src_len = sizeof(src_addr);

    // Cleaning time
    memset(&src_addr, 0, src_len);
    memset(get_buffer, 0, 516);

    do
    {
        ret = recvfrom(socket, get_buffer, 516, 0, (struct sockaddr*) &src_addr, &src_len);
        if(ret == -1)
        {
            printf("Errore in fase di ricezione. \n");
            return;
        }
        else if(!ret)
        {
            printf("Il socket remoto si è chiuso. \n");
            return;
        }

		pkt_len = ret;

        /* Controllo dell'OPCODE */

        memcpy(&opcode, get_buffer, sizeof(opcode));
        opcode = ntohs(opcode);

        if (opcode == ERROR)
        {
            memcpy(&error_type, &get_buffer[2], sizeof(error_type));
            error_type = ntohs(error_type);

            printf("Errore di tipo %d: %s", error_type, &get_buffer[4]);
            return;
        }
        else if (opcode == DATA)
        {
            block ++; 
            memcpy(&pktblock, &get_buffer[2], 2);
            pktblock = ntohs(pktblock);

            /* Primo blocco in arrivo */
            if(block == 1)
            {
                printf("Trasferimento file in corso... \n");
                
                if(strcmp(mode, "txt") == 0)
                    fp = fopen(nome_locale, "a+");
                else if(strcmp(mode, "bin") == 0)
                    fp = fopen(nome_locale, "ab+");               
            }

            /* Arrivo blocchi successivi, controllo che la trasmissione stia andando bene e il pacchetto sia di tipo DATA */

            if(block == pktblock && (ret - 4) > 0)      
            {
                if(strcmp(mode, "txt") == 0)
                {
                    for(i = 0; i < ret - 4; i++)
                        fprintf(fp, "%c", get_buffer[4+i]); // devo scrivere dal 4° byte in poi
                }
                else if (strcmp(mode, "bin") == 0)
                {
                    retwrite = fwrite(&get_buffer[4], 1, ret - 4, fp);
                    if(retwrite < 0)
                    {
                        printf("Errore nella scrittura del file binario. \n");
                        return;
                    }
                }
            }

            /* Creazione e invio del pacchetto di ACK */

            ret = packet_create(ACK, "", "", get_buffer, block);   
            ret = sendto(socket, get_buffer, ret, 0, (struct sockaddr*)&src_addr, src_len);
            if (ret < 0)
            {
                printf("Errore in fase di invio \n");
                return;
            }
        }  
    } while (pkt_len - 4 == 512);       // se pkt_len - 4 < 512 questo è l'ultimo blocco da gestire
    fclose(fp);
    printf("Trasferimento completato (%d/%d blocchi).\n", block, block);
    printf("Salvataggio ./%s completato.\n", nome_locale);
}

int main(int argc, char* argv[])
{
    int sd, ret, len;
    socklen_t server_len;
    struct sockaddr_in server_addr;
    char command_line[100];         // ho bisogno di questa variabile per la fgets
    char command[6];                // la massima lunghezza di un comando è di 5 caratteri
    char mode[4];
    char filename[100]; 
    char nome_locale[100];
    char buffer[516];               // la dimensione massima del pacchetto è 516 (caso di invio di 512 byte)

    // Cleaning time
    memset(filename, 0, strlen(filename));
    memset(nome_locale, 0, strlen(nome_locale));
    memset(buffer, 0, 516);


    /* Il client deve essere avviato con la seguente sintassi:
    ./client.c <IP server> <porta server> */

    if(argc != 3){
        printf("Sintassi errata nell'avvio del client.\n");
        exit(1);
    }

    /* Mostro a schermo i comandi disponibili */

    print_commands();

    /* Creazione socket */

	sd = socket(AF_INET, SOCK_DGRAM, 0);

    // Inizializzazione server_addr
    memset(&server_addr, 0, sizeof(server_addr));   // pulizia
    server_addr.sin_family = AF_INET;               // Usiamo IPv4             
    server_addr.sin_port = htons(atoi(argv[2]));    // Stringa -> Intero -> Host order to Network order
    inet_pton(AF_INET, argv[1], &server_addr.sin_addr); // Presentazione -> Numerico
    server_len = sizeof(server_addr);

    while(1)
    {
        printf("> ");

        /* Devo capire quale comando è stato digitato, uso fgets per ricavare l'intera linea di comando,
        poi estraggo il comando vero e proprio con sscanf e gestisco i vari casi (come se fosse uno switch di stringhe) */

        fgets(command_line, 100, stdin);
        sscanf(command_line, "%s", command);

        /* Gestione dei comandi */

        if(strcmp(command, "!help") == 0)
        {
            print_commands();
        }
        else if(strcmp(command, "!mode") == 0)
        {
            strcpy(mode, mode_control(command_line));
        }
        else if(strcmp(command, "!get") == 0)
        {
            sscanf(command_line, "%s %s %s", command, filename, nome_locale);

            // creo un pacchetto RRQ, il blocco viene passato a 0, perché non serve in questo caso
            len = packet_create(RRQ, mode, filename, buffer, 0);
            ret = sendto(sd, buffer, len, 0, (struct sockaddr*)&server_addr, server_len);
            if (ret < 0){
                printf("Errore in fase di invio \n");
                exit(-1);
            }

            printf("Richiesta file %s al server in corso... \n", filename);
            get_file(sd, mode, nome_locale);
            
            // Cleaning time
            memset(filename, 0, strlen(filename));
            memset(nome_locale, 0, strlen(nome_locale));
            memset(buffer, 0,516);
        }
        else if(strcmp(command, "!quit") == 0)
        {
            close(sd);
            exit(0);
        }
        else
            printf("Comando %s non esistente, usa il comando !help per la lista di comandi. \n", command);      
    }
}

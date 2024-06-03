#ifndef P3HEADER
#define P3HEADER

#include <iostream>
#include <sys/msg.h>
#include <sys/types.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <fcntl.h>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <time.h>

using namespace std;

#define SERVER -1L
#define BSIZE 10
#define LOGSIZE 256
#define SERVER_ADDR "156.12.127.18"
//#define SERVER_ADDR "127.0.0.1"
#define PORT 15003
#define MAX_CLI 30

/** 
 * message struct used for cli/ser communication
 */
typedef struct {
  /** type of msg */
  long msg_type;
  /** sender pid */
  pid_t sender;
  /** request id */
  int request;
  /** data buffer */
  int buffer[BSIZE]; 
  
} MESSAGE;

/**
 * used for sending log file info to client
 */
typedef struct {
  /** type of msg */
  long msg_type;
  /** sender pid */
  pid_t sender;
  /** log msg buffer */
  char buffer[LOGSIZE];
} LOGMSG;

/**
 * contains info about 1 client 
 */
typedef struct {
  /** num of send/recv for client */
  int commands = 0;
  /** client ID, used for allocation */
  int cli = -1;
  /** client's PID */
  pid_t pid;
  /** connection time to server */
  tm start_time;
  /** last send/recv time */
  tm last_time;
} CLI_INFO;

/** 
 * in shared memory, stores info about all clients
*/
typedef struct {
  /** number of clients on machine */
  int num_clis = 0;
  /** array of clients' info */
  CLI_INFO cli_info[MAX_CLI];
  //FILE *logfile;
} CLI_DAT;

// documented in respective cli / ser files
void removeQueue(int);
int getNumRecords();
void sendMessage(MESSAGE *);
void sendMessage(LOGMSG);
void displayRecord(MESSAGE);
void createRecord(MESSAGE);
void modifyRecord(MESSAGE);
void showLog(MESSAGE);
void closeHandler(int);
MESSAGE clearMsg();

// wait()
void P(key_t id, int num) {
  struct sembuf semCmd;
  semCmd.sem_num=num;
  semCmd.sem_op=-1;
  semCmd.sem_flg=SEM_UNDO;
  semop(id,&semCmd,1);
}

// signal()
void V(key_t id, int num) {
  struct sembuf semCmd;
  semCmd.sem_num=num;
  semCmd.sem_op=1;
  semCmd.sem_flg=0;
  semop(id,&semCmd,1);
}

#endif

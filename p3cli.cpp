/**
 * @author     Chloe Kelly
 * @file       p3cli.cpp
 */
#include "p3.hpp"

bool connectToServer();
bool semSetup();
bool shmSetup();
void clientLoop();
void printHeader();
void sendMessage(MESSAGE);
tm *getTime();
void writeLog(string);
void printShm();
void incCommands();

int sem, /*!< semaphore */
  sockfd, /*!< socket for communication */
  shmid, /*!< id of shared memory */
  clinum = -1; /*!< this client's number */
/** pointer to shm */
CLI_DAT *shmptr;
/** info about CURRENT client */
CLI_INFO cli_info;
/** local logfile on this machine */
FILE *logfile;
/** shared memory reader */
#define SHM_READER 0
/** shared memory writer */
#define SHM_WRITER 1
/** client logfile reader */
#define LOG_READER 2
/** client logfile writer */
#define LOG_WRITER 3

/** @brief main function */
int main(int argc, char **argv) {
  if(signal(SIGINT, SIG_IGN) == SIG_ERR) // ignore SIGINT
	perror("signal");	

  if(!connectToServer()) return -1;
  if(!semSetup()) return -1;
  if(!shmSetup()) return -1;

  cout << "Connected to server (client PID: " << getpid() << ")" << endl
       << "Viewing Materials in the U.S. municipal waste stream between 1960 and 2018"
       << endl;

  clientLoop();

  return 0;
}

/** 
 * @brief signal handler for exit, closes sockets
 */
void closeHandler(int sig) {
  writeLog("disconnecting from server");
  P(sem, SHM_WRITER);
  
  shmptr->num_clis--; // remove self from shm
  if(shmptr->num_clis == 0) {
	cout << "Shutting down shm and semaphores on this machine"<< endl;
	shmctl(shmid, IPC_RMID, 0); // clear shm
	semctl(sem, 0, IPC_RMID); // clear sem
  }

  shmptr->cli_info[clinum].cli = -1;
  shmdt(shmptr); // detach shm ptr
  V(sem, SHM_WRITER);
  
  cout << "Sending disconnect msg to server" << endl;
  MESSAGE msg = clearMsg();
  msg.request = 99;
  write(sockfd, &msg, sizeof(MESSAGE)); // tell server we are disconnecting
  
  close(sockfd);
  cout << "Client successfully closed" << endl;
  exit(0);
}

/** 
 * @brief connects to the server on acad, send 'hello' message 
 * @return true if successful, false otherwise
*/
bool connectToServer() {
  struct sockaddr_in server = {AF_INET, htons(PORT), inet_addr(SERVER_ADDR)};
  if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { 
	perror("cannot open socket");
	return false;	  
  }
  if(connect(sockfd, (struct sockaddr *) &server, sizeof(server)) < 0) {
	perror("cannot connect to server");
	return false;
  }

  // say hello, give server our PID
  MESSAGE msg = clearMsg();
  if(write(sockfd, &msg, sizeof(MESSAGE)) < 0) {
	perror("cannot send message to server");
	return false;
  }
  
  return true;
}


/**
 * @brief sets up semaphores. create if needed, otherwise access existing
 * @return true on success
*/
bool semSetup() {
  if((sem = semget(getuid(), 4, 0)) < 0) { // access existing
	if((sem = semget(getuid(), 4, 0666|IPC_CREAT|IPC_EXCL)) < 0) { // create
	  perror("Error creating semaphores");
	  return false;

	} else { 
	  // creator initially blocks all
	  V(sem, SHM_READER);
	  V(sem, SHM_WRITER);
	  V(sem, LOG_READER);
	  V(sem, LOG_WRITER);
	  /*
	  P(sem, SHM_READER);
	  P(sem, SHM_WRITER);
	  P(sem, LOG_READER);
	  P(sem, LOG_WRITER);
	  */
	  clinum = 0;
	}

  } else {
	cout << "Accessing existing semaphores on machine" << endl;
  }
  return true;
}

/**
 * @brief sets up shared memory. create if needed, otherwise access existing
 * @return true on success
*/
bool shmSetup() {
  // WAIT UNTIL ANY OTHER CLIS SET UP SHM BEFORE STARTING
  P(sem, SHM_WRITER);
  bool creator = false;
  // try to create new shm on this machine
  size_t size = sizeof(CLI_DAT);
  if((shmid = shmget(getuid(), size, IPC_CREAT|IPC_EXCL|0600)) < 0) {
	if(errno != EEXIST) { 
	  perror("Error creating shared memory");
	  return false;
	}
	// access existing shm
	if((shmid = shmget(getuid(), size, 0600)) < 0) {
	  perror("Error accessing existing shared memory");
	  return false;
	}
	
  } else { // CREATED SHM
	clinum = 0;
  	creator = true;
  }
  
  shmptr = (CLI_DAT *)shmat(shmid, 0, 0);

  if(clinum == 0) { // creator has to initialize variables in shm
	shmptr->num_clis = 0;
	for(int i=0; i < MAX_CLI; i++)
	  shmptr->cli_info[i].cli = -1;
	/*if((shmptr->logfile = fopen("log.cli", "w")) == NULL) {
	  cout << "Error: Cannot open client log file";
	  return false;
	  } */
	
  } else { // access client number from shared memory
	for(int i=0; i < MAX_CLI; i++) {
	  if(shmptr->cli_info[i].cli == -1) {
		clinum = i;
		break;
	  }
	}
  }
  if(clinum < 0) {
	cout << "Error: Max number of clients already connected to server" << endl;
	return false;
  }

  cli_info.commands = 0;
  cli_info.cli = clinum;
  cli_info.pid = getpid();
  tm *t = getTime();
  cli_info.start_time = *t;
  cli_info.last_time = *t;

  shmptr->cli_info[clinum] = cli_info; // copy local cli_info to shm
  shmptr->num_clis++; // increase client count

  P(sem, LOG_WRITER);
  logfile = fopen("log.cli", "a");
  V(sem, LOG_WRITER);

  if(shmptr->num_clis == 1)
	cout << "Shared memory created on machine" << endl;
  else
	cout << "Accessed existing shared memory on machine" << endl;

  //V(sem, SHM_WRITER); // RELEASE LOCK
  if(creator) {
	V(sem, SHM_READER);
	V(sem, LOG_READER);
	V(sem, LOG_WRITER);
  }
  
  V(sem, SHM_WRITER);
  return true;
}

/** 
 * @brief gives a tm to help with logging operations
 * @return current time
*/
tm *getTime() {
  time_t rawtime;
  struct tm *info;
  char *buffer = (char *)malloc(sizeof(char) * 20);
  time(&rawtime);
  info = localtime(&rawtime);
  //strftime(buffer,20,"%x %H:%M", info);
  return info;
}

/** 
 * @brief creates a new MESSAGE with default values
 * @return the new MESSAGE
 */
MESSAGE clearMsg() {
  MESSAGE msg;
  for(int i=0; i < BSIZE; i++)
	msg.buffer[i] = 0;
  msg.sender = getpid();
  msg.msg_type = 1;
  msg.request = -1;
  return msg;
}


/** 
 * @brief displays menu to client
 */
void showMenu() {
  cout << endl;
  cout << "1) Create New Record" << endl;
  cout << "2) Display Record" << endl;
  cout << "3) Modify Record" << endl;
  cout << "4) Show Log" << endl;
  cout << "5) Show Local Clients" << endl;
  cout << "(-1 to quit)" << endl;
}


/** 
 * @brief main loop for handling client's requests
 */
void clientLoop() {
  writeLog("connected to server, waiting for user input");
  while(true) {
	MESSAGE msg = clearMsg();
	int input;
	
	showMenu();

	cin >> input;

	switch(input) {
	case 1: // create
	  createRecord(msg);
	  break;
	  
	case 2: // display record
	  displayRecord(msg);
	  break;

	case 3: // modify
	  modifyRecord(msg);
	  break;

	case 4: // log
	  showLog(msg);
	  break;

	case 5: // view clients
	  printShm();
	  break;

	case -1: // quit
	  //kill(getpid(), SIGINT);
	  closeHandler(-1);
	  exit(0);
	  return;
	  
	default:
	  cout << "Invalid choice" << endl;
	} // end switch
  } // end while
} // end clientLoop()


/** 
 * @brief displays header for when printing record
 */
void printHeader() {
  cout << "Materials in the U.S. municipal waste stream between 1960 and 2018" << endl;
  cout << "(in 1000 tons)" << endl;
  cout << "----------------------------------" << endl;
  cout << left << setw(6) << "Year"
	   << left << setw(10) << "Paper"
	   << left << setw(10) << "Glass"
	   << left << setw(10) << "Metals"
	   << left << setw(10) << "Plastics"
	   << left << setw(10) << "Rubber"
	   << left << setw(10) << "Textiles"
	   << left << setw(10) << "Wood"
	   << left << setw(10) << "Other" << endl;
}


/** 
 * @brief sends the given message to the server
 * @param msg the message to be sent
 */
void sendMessage(MESSAGE msg) {
  if(write(sockfd, &msg, sizeof(MESSAGE)) < 0) {
	perror("cannot send message to server");
	closeHandler(-1);
	exit(-1);
  }
  incCommands();
}


/** 
 * @brief waits for server to send number of records
 * @return number of records
 */
int getNumRecords() {
  MESSAGE msg;
  if(read(sockfd, &msg, sizeof(MESSAGE)) == -1) {
	perror("getnumrecords read");
	closeHandler(-1);
	exit(-1);
  }
  incCommands();
  return msg.request;
}


/** 
 * @brief prompts user for info about new record, then sends it to server
 * @param msg the message to be sent
 */
void createRecord(MESSAGE msg) {
  msg = clearMsg();
  msg.request = 1;
  cout << "Enter the data for a new record (in 1000 tons) as integers" << endl;
  string fields[9] = {"Year", "Paper", "Glass", "Metals",
	"Plastics", "Rubber", "Textiles", "Wood", "Other"};

  for(int i=0; i < 9; i++) {
	int input;
	cout << fields[i] << ": ";
	cin >> input;
	msg.buffer[i] = input;
	cin.clear();
  }
  sendMessage(msg);

  MESSAGE msg_recv;
  if(read(sockfd, &msg_recv, sizeof(MESSAGE)) == -1) { // get 1 record
	perror("error getting creation acknowledgement");
	closeHandler(-1);
	exit(-1);
  }
  incCommands();
  
  cout << "Server confirms record created" << endl << endl;
  writeLog("created new record");
}


/** 
 * @brief prompts the user to select a record, then displays it
 * @param msg the message to be sent
 */
void displayRecord(MESSAGE msg) {
  // get number of records from server
  msg = clearMsg();
  msg.request = 10;
  sendMessage(msg);
  int numRecords = getNumRecords();

  // ask user to select a record
  int rNum = -1;
  while(rNum != -999 && (rNum < 1 || rNum > numRecords)) {
	cout << "Enter a record number between 1-" << numRecords << " (-999 for all): ";
	cin >> rNum;
  }
  if(rNum != -999)
	numRecords = 1;
  
  // tell server which record(s) to send
  msg = clearMsg();
  msg.request = 2;
  msg.buffer[0] = rNum;
  sendMessage(msg); 

  cout << endl;
  printHeader();

  for(int i=0; i < numRecords; i++) { // get all records requested
	MESSAGE msg_recv;
	if(read(sockfd, &msg_recv, sizeof(MESSAGE)) == -1) { // get 1 record
	  perror("get record read");
	  closeHandler(-1);
	  exit(-1);
	}
	incCommands();
	
	for(int i=0; i < 9; i++) { // print 1 record
	  if(i%9 == 0) cout << setw(6);
	  else cout << setw(10);
	  cout << left << msg_recv.buffer[i];
	  if(i%9 == 8) cout << endl;
	} // end for

  } // end for
  cout << "----------------------------------" << endl << endl;
  writeLog("requested to view record #" + to_string(rNum));
} // end displayMessage


/** 
 * @brief prompts user to select a record, then modify it
 * @param msg the message to be sent
 */
void modifyRecord(MESSAGE msg) {
  msg = clearMsg();
  msg.request = 10;
  sendMessage(msg); // ask for # of records

  MESSAGE msg_recv;
  int numRecords = getNumRecords();
  int rNum = -1;
  while(rNum < 1 || rNum > numRecords) {
	cout << "Enter a record number between 1-" << numRecords << ": ";
	cin >> rNum;
  }

  // ask server for record number 'rNum'
  //clearMsg(msg);
  msg.request = 2;
  msg.buffer[0] = rNum;
  sendMessage(msg);

  if(read(sockfd, &msg_recv, sizeof(MESSAGE)) == -1) {
	perror("read");
	closeHandler(-1);
	exit(-1);
  }
  incCommands();
	
  // show record from server
  cout << endl;
  printHeader();
  for(int i=0; i < 9; i++) {
	if(i%9 == 0) cout << setw(6);
	else cout << setw(10);
	cout << left << msg_recv.buffer[i];
	if(i%9 == 8) cout << endl;
  }
  cout << "----------------------------------" << endl << endl;
  
  // display menu to user
  string fields[9] = {"Year", "Paper", "Glass", "Metals",
	"Plastics", "Rubber", "Textiles", "Wood", "Other"};
  for(int i=0; i < 9; i++)
	cout << i << ") " << fields[i] << endl; 
  cout << endl;
  
  int val, field = -1;
  while(field < 0 || field >= 9) { 
	cout << "Select a field to modify (0-8): ";
	cin >> field;
  }

  cout << endl << "Enter a new value for '" << fields[field] << "': ";
  cin >> val;

  // send 'modifyRecord' request
  //clearMsg(msg);
  msg.buffer[9] = rNum - 1;
  msg.request = 3;

  for(int i=0; i < 9; i++) {
	if(i == field)
	  msg.buffer[i] = val; // new value
	else
	  msg.buffer[i] = msg_recv.buffer[i]; // old value
  }
  
  sendMessage(msg);

  if(read(sockfd, &msg, sizeof(MESSAGE)) == -1) {
	perror("error getting modify acknowledgement");
	closeHandler(-1);
	exit(-1);
  }
  incCommands();
  
  cout << "Server confirms record modified" << endl << endl;
  writeLog("modified record #" + to_string(rNum));
} // end modifyRecord


/** 
 * @brief receives logs from server through multiple transmissions
 * @param msg the message to be sent
 */
void showLog(MESSAGE msg) {
  msg.request = 4;
  sendMessage(msg); 
  // server responds with # of log messages
  if(read(sockfd, &msg, sizeof(MESSAGE)) == -1) {
	perror("read");
	closeHandler(-1);
	exit(-1);
  }
  incCommands();
	
  int numLogs = msg.request; // number of times server needs to send LOGMSG
  LOGMSG log;
  log.sender = getpid();
  log.msg_type = 1;
  
  for(int i=0; i < numLogs; i++) { // one LOGMSG is 1 line in the logfile
	memset(log.buffer, ' ', LOGSIZE);
	if(read(sockfd, &log, sizeof(LOGMSG)) == -1) {
	  perror("read");
	  closeHandler(-1);
	  exit(-1);
	}
	cout << log.buffer << endl;
  }
  incCommands();
  writeLog("displayed server's log file");
}

/** 
 * @brief displays contents of ALL shared memory on this machine
 */
void printShm() {
  cout << "-------------------------" << endl;
  cout << "Shared memory contents: " << endl << endl;
  cout << left << setw(12) << "" << setw(5) << "id"
	   << setw(5) << "#" << setw(10) << "pid"
	   << setw(20) << "start time" << setw(20) << "last msg time" << endl;
  CLI_INFO c;

  P(sem, SHM_WRITER);
  
  for(int i=0; i < MAX_CLI; i++) {
	c = shmptr->cli_info[i];
	if(c.cli != -1) {
	  char start[20], last[20];
	  
	  strftime(start,20,"%x %H:%M:%S", &c.start_time);
	  strftime(last,20,"%x %H:%M:%S", &c.last_time);
	  cout << left << setw(12) << "CLI_INFO: " << setw(5) << c.cli
		   << setw(5) << c.commands << setw(10) << c.pid
		   << setw(20) << start << setw(20) << last << endl;
	}

  }
  cout << "-------------------------" << endl;

  V(sem, SHM_WRITER);
}

/** 
 * @brief writes to the LOCAL client log file
 * @param s text to be written
 */
void writeLog(string s) {
  string str = "PID: " + to_string(getpid()) + " | " + s + "\n";
  P(sem, LOG_WRITER);
  fseek(logfile, 0, SEEK_END); // unnecessary
  fwrite(str.c_str(), sizeof(char) * str.length(), 1, logfile);
  V(sem, LOG_WRITER);
}

/**
 * @brief increments number of commands (in shm) for this client 
 */
void incCommands() {
  cli_info.commands++;
  tm *t = getTime();
  cli_info.last_time = *t;
  
  P(sem, SHM_WRITER);
  shmptr->cli_info[clinum] = cli_info;
  V(sem, SHM_WRITER);
}

/** 
 * \mainpage 
 * <h2> Notes </h2>
 * <p> When implementing the R/W algorithm, I ran into a lot of 
 * problems. Originally I attempted to put the read/write count into
 * shared memory on the server, and another semaphore for 
 * accessing it. I also tried just using a pointer, but also had issues.
 * There was obviously something wrong with my algorithm: it would crash
 * or infinitely loop when running multiple scripts at a time. 
 * Therefore, in order to get synchronization to work 100% of the time,
 * I got rid of a bunch of code and only allowed 1 access to the
 * protected data at a time. This way, I haven't run into any major
 * bugs, but it is inefficient. </p>
 * <h2> Shared Memory </h2>
 * <p>The first client on a machine will create shared memory. A CLI_DAT struct
 * is used to store all client information. The CLI_DAT contains the number of
 * clients currently on this machine and an array of CLI_INFO. </p>
 * <p>CLI_INFO stores info about a single client. This contains the number of
 * send/recvs by the client, their PID, connection time, last command time,
 * and a client ID. The client ID is used to determine this client's position in
 * the CLI_INFO array within CLI_DAT. The first client has an ID of 0, then any
 * more clients that connect will loop through the CLI_INFO array until they
 * find one with an ID of -1 (indicating it's available). When a client 
 * disconnects, they set their ID back to -1. If they are the last client on
 * the machine, they remove all shared memory. </p>
 * <h2> Semaphores </h2>
 * <p> Semaphores are used on both the client and server to prevent race conditions
 * when accessing shared memory, logfiles, or the binary data file. </p>
 * <p> P() and V() are defined in p3.hpp and will block / signal, respectively. 
 * The setup works similar to the shared memory, if sempahores do not exist then
 * the process will create them, and remove on disconnect if necessary. Both the
 * client and the server keep a log, clients on the same machine share a logfile. </p>
 * <h2> </h2>
 * <h2> Message Request ID Information </h2>
 * <p>The MESSAGE contains an "int request" that identifies which operation
 *    the client wants to perform: </p>
 * <table> 
 * <tr>
 * <td>1</td> <td>create a record </td>
 * </tr> <tr>
 * <td>2</td> <td>display a record </td>
 * </tr> <tr>
 * <td>3</td> <td>modify a record </td>
 * </tr> <tr>
 * <td>4</td> <td>show log file </td>
 * </tr> <tr>
 * <td>10</td> <td>get number of records </td>
 * </tr>
 * </table>
 * <p>Clients may also display the contents of the shared memory on their machine.
 * This is completely local, so no message needs to be sent to the server </p>
 * <h2>Operation Descriptions</h2>
 * <h4>Create Record</h4>
 * <p> The client is asked for the 9 data fields for the new record. 
 * Those fields are stored in the MESSAGE buffer and sent to server.
 * The server then inserts the record into the data file and sends a confirmation
 * of success back to the client.
 * Finally, the client recieves the confirmation and goes back to the main loop. 
 * </p>
 * <h4>Display Record</h4>
 * <p> The client asks the server how many records exist. They are then prompted to
 * enter a record number, which is sent back to the server. The server looks up
 * the client's requested record and fills the MESSAGE buffer with it. Then the 
 * client recieves the record and prints it.
 * If -999 is entered (display all), the server once again sends
 * the number of records (in case it has been modified in the meantime). The client then
 * loops for the number of records: each record is sent in a different msgsnd.
 * </p>
 * <h4>Modify Record</h4>
 * The client requests the number of records. Then they are prompted to enter the record
 * number to modify. The 9 data fields are displayed, and the client selects one.
 * They enter the new value, and the MESSAGE buffer is filled with the entire record, 
 * with the old value replaced. The server then sends a confirmation of success.
 * <h4>Get Number of Records</h4>
 * The client requests the number of records in the data file. This is used
 * within the other Requests multiple times, so a separate request is easier. 
 * <h4>Show Log</h4>
 * When sending the log file, only 1 line is sent at a time through a LOGMSG.
 * The server first sends the number of lines in the file to the client using a MESSAGE,
 * then uses the LOGMSG's character array to send 1 line, looping until the entire file
 * has been sent.
 * <h4>Show Local Clients</h4>
 * Displays the contents of the shared memory on this machine.
 */

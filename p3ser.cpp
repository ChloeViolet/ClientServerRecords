/**
 * @author     Chloe Kelly
 * @file       p3ser.cpp
*/
#include "p3.hpp"

bool startServer();
void serverListen();
void handleClient();
void handleRequest(MESSAGE);
void sendNumRecords();
void writeLog(pid_t, string);
void childCatcher(int);
void intCatcher(int);


/** binary data file for operations */
FILE *file;
/** server logfile */
fstream logfile;
int sockfd, /*!< socket for listening */
  newsockfd, /*!< client / child's socket */
  cliPID; /*!< client's PID */
/** client's IP */
char *cliIP;
/** semaphore */
int sem;
//int readerCount = 0, writerCount = 0;
/** number of children active */
int childCount = 0;
/** key for semaphores */
key_t semKey;
/** current pid */
int pid = -1;
/** parent's pid */
int parentPID = -1;
/** datafile reader */
#define D_READER 0
/** datafile writer */
#define D_WRITER 1
/** logfile reader */
#define L_READER 2
/** logfile reader */
#define L_WRITER 3

/** @brief main function */
int main(int argc, char **argv) {
  //if(signal(SIGINT, closeHandler) == SIG_ERR)
  if(signal(SIGINT, SIG_IGN) == SIG_ERR) // ignore SIGINT
	perror("signal");	

  cout << "Opening data file" << endl;
  if((file = fopen("CSC552p3.bin", "rb+")) == NULL) {
	cout << "Error: Cannot open binary data file" << endl;
	return -1;
  }
  cout << "Opening log file" << endl;
  logfile.open("log.ser", fstream::in | fstream::app);
  if(logfile.fail()) {
	cout << "Error: Cannot open log file" << endl;
	return -1;
  }
  
	
  cout << "Starting server..." << endl;
  if(!startServer()) { closeHandler(-1); return -1; }

  cout << "Waiting for clients..." << endl << endl;
  serverListen();
  
  return 0;
} // end main

/** 
 * @brief starts up the server socket 
 * @return true on success
*/
bool startServer() {
  // create semaphores for binary file and log
  semKey = PORT;
  parentPID = getpid();
  if((sem = semget(semKey, 4, 0666|IPC_CREAT)) < 0) {
	perror("cannot create semaphores");
	return false;
  }
  // TODO: FIX
  V(sem, D_READER);
  V(sem, D_WRITER);
  V(sem, L_READER);
  V(sem, L_WRITER);
  
  struct sockaddr_in server = {AF_INET, htons(PORT), INADDR_ANY};
  // create socket, bind it, listen
  if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { 
	perror("cannot open socket");
	return false;	  
  }
  if(bind(sockfd, (struct sockaddr *) &server, sizeof(server)) < 0) {
	perror("bind error");
	return false;
  }
  if(listen(sockfd, 5) == -1) {
	perror("listen failed");
    return false;
  }
  // display current IP address of server
  char host[256];
  int hostname = gethostname(host, sizeof(host));
  struct hostent *host_entry = gethostbyname(host);  
  char *IP = inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0]));

  cout << "-------------------------------------" << endl;
  cout << "Server started on '" << IP << "'" << endl;
  cout << "-------------------------------------" << endl;

  return true;
}

/** 
 * @brief signal handler for exit/kill, closes sockets
*/
void closeHandler(int sig) {
  cout << "Server closing..." << endl;
  if(pid != 0) { // if parent
	semctl(sem, 0, IPC_RMID);
	close(newsockfd);
	close(sockfd);
	logfile.close();
	fclose(file);
	exit(0);
  } else { // if child
	//kill(parentPID, SIGCHLD);
  }
  
}

/** 
 * @brief main loop for the server, listens for incoming clients 
*/
void serverListen() {
  signal(SIGCHLD, childCatcher); // wait for children to close
  
  while(true) { // wait for connections
	struct sockaddr_in cli;
	socklen_t clilen = sizeof(cli);
	/*
	fd_set set;
	FD_ZERO(&set);
	FD_SET(sockfd, &set);
	int result = select(sockfd, &set, NULL, NULL, NULL);
	if(result < 0) {
	  perror("select error");
	  exit(-1);
	  } */

	// wait for incoming clients
	signal(SIGINT, intCatcher); // unblock SIGINT
	while(((newsockfd = accept(sockfd, (struct sockaddr *) &cli, &clilen)) < 0)) ;
	signal(SIGINT, SIG_IGN); // reblock

	
	if(newsockfd < 0) {
	  perror("accept error");
	  exit(-1);
	}
	if((pid = fork()) < 0) {
	  perror("fork error");
	  exit(-1);
	  
	} else if(pid == 0) { // child
	  cliIP = inet_ntoa(cli.sin_addr);
	  close(sockfd);
	  handleClient();
	  close(newsockfd);
	  exit(0);
	  
	} else { // parent
	  close(newsockfd);
	  childCount++;
	}	
	
  } // end while
} // end serverListen

/** 
 * @brief child server handles 1 client. main loop
*/
void handleClient() {
  MESSAGE hello; // get the PID from the client
  if(read(newsockfd, &hello, sizeof(MESSAGE)) == -1) {
	  perror("read");
	  return;
  }
  cliPID = hello.sender;
  cout << "[" << cliPID << "]: " << "client connected from " << cliIP << endl;
  string ip(cliIP);
  writeLog(cliPID, "connected from [" + ip + "]");
  
  // main loop
  while(true) {
	MESSAGE msg;
	// SIGINT is blocked. Only want parent server handling it.
	//signal(SIGINT, intCatcher); // unblock signal
	if(read(newsockfd, &msg, sizeof(MESSAGE)) < 0) {
	  perror("read");
	  return;
	}
	//signal(SIGINT, SIG_IGN); // block
	
	if(msg.request == 99) {
	  cout << "[" << cliPID << "]: client requests disconnect" << endl;
	  kill(parentPID, SIGCHLD);
	  return;
		//exit(0);
	}

	//cout << "[" << cliPID << "]: received " << msg.request << endl;
	handleRequest(msg);
  }
}

/** 
 * @brief processes a client's request
 * @param msg message from the client
*/
void handleRequest(MESSAGE msg) {
  //MESSAGE *msg;
  switch(msg.request) {
  case 1: // create new record
	cout << "received createRecord" << endl;
	writeLog(msg.sender, "requesting to create record");
	createRecord(msg);
	break;
	
  case 2: // display a record
	cout << "received displayRecord" << endl;
	writeLog(msg.sender, "requesting to display records");
	displayRecord(msg);
	break;

  case 3: // modify record
	cout << "received modifyRecord" << endl;
	writeLog(msg.sender, "requesting to modify record");
	modifyRecord(msg);
	break;

  case 4: // log
	cout << "received showlog" << endl;
	writeLog(msg.sender, "requesting to send log file");
	showLog(msg);
	break;
	
  case 10: // send num records
	cout << "received numRecords" << endl;
	writeLog(msg.sender, "requesting number of records");
	sendNumRecords();
	break;

  default:
	cout << "Client sent invalid request number: " << msg.request << endl;
	exit(0);
	break;
  }
  
}


/** 
 * @brief sends a message to the client
 * @param msg the message to be sent
*/
void sendMessage(MESSAGE msg) {
  if(write(newsockfd, &msg, sizeof(MESSAGE)) < 0) {
	perror("cannot send message to client");
	exit(-1);
  }  
}

/** 
 * @brief sends a LOGMSG to the client
 * @param log the LOGMSG to be sent
*/
void sendMessage(LOGMSG log) {
  if(write(newsockfd, &log, sizeof(LOGMSG)) < 0) {
	perror("cannot send LOGMSG to client");
	exit(-1);
  }  
}

/** 
 * @brief sends the number of records in the data file to client
*/
void sendNumRecords() {
  int n = getNumRecords();
  cout << "num records: " << n << endl;
  MESSAGE msg = clearMsg();
  msg.request = n;
  sendMessage(msg);
  writeLog(cliPID, "sending number of records");
}


/** 
 * @brief calculates number of records in data file
 * @return number of records
*/
int getNumRecords() {
  P(sem, D_WRITER);
  fseek(file, 0, SEEK_END);
  int fsize = ftell(file);
  int numRecords = fsize / 9 / sizeof(int); // 9 ints per row
  V(sem, D_WRITER);
  return numRecords;
}


/** 
 * @brief handles a createRecord request from the client
 * @param msg message from the client with new record data inside
*/
void createRecord(MESSAGE msg) {
  writeLog(msg.sender, "creating new record");

  P(sem, D_WRITER);
  
  fseek(file, 0, SEEK_END);
  fwrite(msg.buffer, sizeof(int) * 9, 1, file);

  V(sem, D_WRITER);
  
  sendMessage(msg); // send acknowledgement of creation
  writeLog(msg.sender, "sent record-created confirmation");
}


/** 
 * @brief handles a displayRecord request from the client
 * @param msg message from the client
*/
void displayRecord(MESSAGE msg) {
  int rNum = msg.buffer[0]; // record number to read

  
  if(rNum == -999) { // send all records
	writeLog(msg.sender, "sending ALL records to client");
	int numRecords = getNumRecords();
	P(sem, D_WRITER);
	for(int j=0; j < numRecords; j++) {
	  fseek(file, j * sizeof(int) * 9, SEEK_SET);
	  int line[9];
	  fread(line, sizeof(int)*9, 1, file); // read record from file

	  for(int i=0; i < 9; i++)
		msg.buffer[i] = line[i]; // copy record to message

	  sendMessage(msg);	  
	}
	V(sem, D_WRITER);
	
  } else { // only send 1 record
	writeLog(msg.sender, "sending 1 record to client");
	P(sem, D_WRITER);
	fseek(file, (rNum-1) * sizeof(int) * 9, SEEK_SET);
	int line[9];
	fread(line, sizeof(int)*9, 1, file); // read record from file

	for(int i=0; i < 9; i++)
	  msg.buffer[i] = line[i]; // copy record to message
	sendMessage(msg);
	V(sem, D_WRITER);
  }
} // end displayRecord


/** 
 * @brief handles a modify-record request from the client
 * @param msg message from the client
*/
void modifyRecord(MESSAGE msg) {
  int recordNum = msg.buffer[9];
  int record[9];
  for(int i=0; i < 9; i++) {
	record[i] = msg.buffer[i];
  }
  
  writeLog(msg.sender, "modifying record");

  P(sem, D_WRITER);
  
  fseek(file, recordNum * sizeof(int) * 9, SEEK_SET); // go to record
  fwrite(record, sizeof(int) * 9, 1, file);

  V(sem, D_WRITER);
  
  sendMessage(msg); // send acknowledgement of creation
  writeLog(msg.sender, "sent record-modified confirmation");  

}


/** 
 * @brief sends logs to client through multiple transmissions
 * @param msg message from client
*/
void showLog(MESSAGE msg) {
  int lineCount = 0;
  string line;

  P(sem, L_WRITER);
  
  logfile.clear(); // reset EOF flag
  logfile.seekg(0); // back to start
  while(getline(logfile, line)) // count lines in file
	lineCount++;

  V(sem, L_WRITER);
  cout << "sending " + to_string(lineCount) + " log messages" << endl;

  msg.request = lineCount;
  sendMessage(msg); // tell client how many LOGMSG to expect

  LOGMSG log;
  log.msg_type = msg.sender; // necessary??

  P(sem, L_WRITER);
  logfile.clear();
  logfile.seekg(0); // back to start
  while(getline(logfile, line)) { // send each line to the client
	memset(log.buffer, ' ', LOGSIZE); // unnecessary?
	strcpy(log.buffer, line.c_str());
	sendMessage(log);
  }

  V(sem, L_WRITER);
  
  writeLog(msg.sender, "sent " + to_string(lineCount) + " log messages");
}


/** 
 * @brief writes server status to a logfile (appending)
 * @param client the client's PID
 * @param request information about the operation performed
*/
void writeLog(pid_t client, string request) {
  P(sem, L_WRITER); // wait

  logfile.clear();
  logfile.seekp(0, ios::end);
  logfile << "Client PID: " << client << " | Operation: " << request << endl;

  V(sem, L_WRITER); // signal
}

/** 
 * @brief handles when a child disconnects from the parent
 * @param sig signal
*/
void childCatcher(int sig) {
  pid_t pid;
  int stat;
  while((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
	childCount--;
	cout << "Server: Child " << pid << " terminated" << endl;
	writeLog(pid, "client disconnected");
	return;
  } //end while

} //end childCatcher

/** 
 * @brief handles interrupt signals
 * @param sig signal
*/
void intCatcher(int sig) {
  if(getpid() != parentPID) { // child server
	cout << "closing child data_server" << endl;
	closeHandler(-1);
	exit(0);
	
  } else if(childCount == 0) { // parent server, no clients connected
	string input;
	cout << "No clients connected. Are you sure you want to quit? (y/n): ";
	cin >> input;
	if(input[0] == 'y' || input[0] == 'Y') {
	  cout << "Goodbye!" << endl;
	  closeHandler(-1);
	  exit(0);
	}
	
  } else { // parent, clients are connected
	cout << "There are " << childCount << " clients connected. SIGINT ignored" << endl;
  }

} //end childCatcher

/** 
 * @brief gives a MESSAGE with default values set
 * @return new empty message
 */
MESSAGE clearMsg() {
  MESSAGE msg;
  for(int i=0; i < BSIZE; i++)
	msg.buffer[i] = 0;
  msg.sender = cliPID;
  msg.msg_type = 1;
  msg.request = -1;
  return msg;
}


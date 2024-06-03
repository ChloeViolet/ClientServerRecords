# Author:     Chloe Kelly
# Filename:   README.txt

Usage:
	make all - compiles p3ser.cpp and p3cli.cpp
	make client - only compiles client
	make server - only compiles server

	./cli.sh will use inputA inputB and inputC so 3 different
	clients are sending commands to the server at once.
	(assuming the server is running)

	No command line arguments to ./server or ./client

---------------------------------
Doxygen Link:

^^^^^^^^^^^^^^^^^^^^^^^^^^^^
A description of commands is on the mainpage of the doxygen site
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  
The test script (cli.sh) uses 3 input files, each of which uses different
operations (create, display, modify, show log, show local shm).
  
The client simply loops and waits for the user to enter a request, then it performs
each request as described on the Doxygen page.
The server works in a similar way and uses the MESSAGE's request number to determine
the next steps for each operation.
Every time the server receives something from the client, sends something, or
modifies the binary data file, a message is written to the log file "log.ser"
where the client's PID and the operation is noted.
The client also keeps 1 logfile per machine to keep track of operations.

The file "p3.hpp" has functions that both cli + server implement, such as
getNumRecords and displayRecord, but their implementation is obviously
completely different, with the server sending record data and the client receiving.

-------------------------------
Known Bugs:

When running the test script in bulk ('./cli.sh & ./cli.sh & ./cli.sh), the server
  sometimes does not correctly keep track of the number of clients.
  This results in issues with SIGINT on the server, since it still thinks there are
  clients connected, and it must be manually killed.
    
  
Server occasionally likes to fail with "invalid request number", forcing a manual kill.
  I believe this happens when a sigchld is received while waiting on an accept.
  I am 95% sure I fixed this bug: haven't been able to replicate it for several days now.
  Could still be an issue.
  

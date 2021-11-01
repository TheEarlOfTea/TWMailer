#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <iostream>
#include <filesystem>
#include <fstream>


///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
/* the number of files the user has received is stored in a file in their directory named numOfFiles */
#define COUNTERFILENAME "numOfFiles.txt" 
// #define PORT 6543
#define SEPERATOR ";"
#define MAX_NAME 8
#define MAX_SUBJ 80

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;
using namespace std;
namespace fs = std::filesystem;

///////////////////////////////////////////////////////////////////////////////

void *clientCommunication(void *data, string folder);
bool receiveFromClient(string buffer, string folder);
string list(string buffer, string folder);
string read(string buffer, string folder);
bool deleteMessage(string buffer, string folder);

/* HELPERS */
void signalHandler(int sig);
void printUsage();
int getNumOfFiles(string folder);
string getHighestFileNumber(string folder);
string getString(string buffer);
string removeString(string buffer, string s1);
bool verifyStringLength(string string, int maxStringLength);


///////////////////////////////////////////////////////////////////////////////

/**
 * @brief Server for basic tw-mailer. Responds to commands from the client, can save a message in the receiver's repository,
 * list the number of messages in the user's inbox and their subjects, read the content of a given message and delete messages
 * 
 */
int main(int argc, char* argv[])
{
   /* ARGUMENT HANDLING */
   if(argc != 3) { 
      printUsage();
   }

   string folder = argv[2];

   try {
      if (!fs::is_directory(folder)) { /* given persistence folder does not exist and needs to be created */
         cout << folder << " does not exist. Creating now..." << endl;
         fs::create_directory(folder);
      } 
   } catch(fs::filesystem_error& error) {
      cerr << error.what() << endl;
      exit(EXIT_FAILURE);
   }

   /* END OF ARGUMENT HANDLING */

   socklen_t addrlen;
   struct sockaddr_in address, cliaddress;
   int reuseValue = 1;
   

   /* SIGNAL HANDLER SIGINT (Interrupt: ctrl+c) */
   if (signal(SIGINT, signalHandler) == SIG_ERR) {
      perror("signal can not be registered");
      return EXIT_FAILURE;
   }

   /* CREATE A SOCKET IPv4, TCP (connection oriented), IP (same as client) */
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror("Socket error"); // errno set by socket()
      return EXIT_FAILURE;
   }

   /* SET SOCKET OPTIONS socket, level, optname, optvalue, optlen */
   if (setsockopt(create_socket,
                  SOL_SOCKET,
                  SO_REUSEADDR,
                  &reuseValue,
                  sizeof(reuseValue)) == -1) {
      perror("set socket options - reuseAddr");
      return EXIT_FAILURE;
   }

   if (setsockopt(create_socket,
                  SOL_SOCKET,
                  SO_REUSEPORT,
                  &reuseValue,
                  sizeof(reuseValue)) == -1) {
      perror("set socket options - reusePort");
      return EXIT_FAILURE;
   }

   /* INIT ADDRESS Attention: network byte order => big endian */
   memset(&address, 0, sizeof(address));
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;

   try {
      /* checks if port is in suitable range */
      if(stoi(argv[1]) < 1024 || stoi(argv[1]) > 65535) {
         cerr << "Input Port is not in usable port range" << endl;
         exit(EXIT_FAILURE);
     }
     
   } catch (invalid_argument& e1) { /* exits, if input port is NAN */
      cerr <<"Port was not a number" << endl;
      exit(EXIT_FAILURE);
   }

   address.sin_port = htons(stoi(argv[1]));

   /* ASSIGN AN ADDRESS WITH PORT TO SOCKET */
   if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
      perror("bind error");
      return EXIT_FAILURE;
   }

   /* ALLOW CONNECTION ESTABLISHING Socket, Backlog (= count of waiting connections allowed) */
   if (listen(create_socket, 5) == -1) {
      perror("listen error");
      return EXIT_FAILURE;
   }

   while (!abortRequested)
   {
      /* ignore errors here... because only information message */
      printf("Waiting for connections...\n");

      
      /* ACCEPTS CONNECTION SETUP blocking, might have an accept-error on ctrl+c */
      addrlen = sizeof(struct sockaddr_in);
      if ((new_socket = accept(create_socket,
                               (struct sockaddr *)&cliaddress,
                               &addrlen)) == -1) {
         if (abortRequested) {
            perror("accept error after aborted");
         }
         else {
            perror("accept error");
         }
         break;
      }

      /* START CLIENT ignore printf error handling */
      printf("Client connected from %s:%d...\n",
             inet_ntoa(cliaddress.sin_addr),
             ntohs(cliaddress.sin_port));
      clientCommunication(&new_socket, folder); // returnValue can be ignored
      new_socket = -1;
   }

   // frees the descriptor
   if (create_socket != -1) {
      if (shutdown(create_socket, SHUT_RDWR) == -1) {
         perror("shutdown create_socket");
      }
      if (close(create_socket) == -1) {
         perror("close create_socket");
      }
      create_socket = -1;
   }

   return EXIT_SUCCESS;
}

void *clientCommunication(void *data, string folder)
{
   char buffer[BUF];
   int size;
   int *current_socket = (int *)data;

   /* SEND welcome message */
   strcpy(buffer, "Welcome to the server!\r\nPlease enter your commands...\r\n");
   if (send(*current_socket, buffer, strlen(buffer), 0) == -1) {
      perror("send failed");
      return NULL;
   }


   do {
      /* RECEIVE */
      size = recv(*current_socket, buffer, BUF - 1, 0);
      if (size == -1) {
         if (abortRequested) {
            perror("recv error after aborted");
         }
         else {
            perror("recv error");
         }
         break;
      }

      if (size == 0) {
         printf("Client closed remote socket\n"); // ignore error
         break;
      }

      // remove ugly debug message, because of the sent newline of client
      if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n') {
         size -= 2;
      }
      else if (buffer[size - 1] == '\n') {
         --size;
      }

      buffer[size] = '\0';
      printf("Message received: %s\n", buffer); // ignore error

      /* first char of the buffer is the flag sent from the client */
      char flag = buffer[0];
      string bufferString = "";
      bufferString += buffer;
      string response = ""; /* response to the client upon the given request */
      

      switch(flag){
         case 's': /* SEND */
               response = receiveFromClient(bufferString, folder) ? "OK" : "ERR";
            break;
         case 'l': /* LIST */
               response = list(bufferString, folder);
            break;
         case 'r': /* READ */
               response = read(buffer, folder);
            break;
         case 'd': /* DELETE */
               response = deleteMessage(buffer, folder) ? "OK" : "ERR";
            break;
         case '?':
            perror("Invalid command");
            break;
         default: 
            break;
      }

      if (send(*current_socket, response.c_str(), strlen(response.c_str()), 0) == -1)
      {
         perror("send answer failed");
         return NULL;
      }
      

   } while (strcmp(buffer, "quit") != 0 && !abortRequested);

   // closes/frees the descriptor if not already
   if (*current_socket != -1) {
      if (shutdown(*current_socket, SHUT_RDWR) == -1) {
         perror("shutdown new_socket");
      }
      if (close(*current_socket) == -1) {
         perror("close new_socket");
      }
      *current_socket = -1;
   }

   return NULL;
}

/**
 * @brief Responsible for SEND requests from the client. First the string gets split up and sender, receiver, subject, message are 
 * saved accordingly. Then the server checks if the receiver already has a directory for their messages: if not then one is set up for them.
 * Messages each get their own file in the receiver's directory and are named 1.txt, 2.txt, etc.
 * 
 * @param buffer string received from client in the form of s;sender;receiver;subject;message
 * @param folder given directory where messages should be persisted
 * @return true = receive worked/ OK, false = something went wrong/ ERR
 */
bool receiveFromClient(string buffer, string folder){

   string sender, receiver, subject, message;

   
   buffer = removeString(buffer, "s"); /* remove flag at the beginning of the package */

   sender = getString(buffer); /* get sender */
   if(!verifyStringLength(sender, MAX_NAME)) {
      return false;
   }
   buffer = removeString(buffer, sender);
   
   receiver = getString(buffer); /* get receiver */
   if(!verifyStringLength(receiver, MAX_NAME)) {
      return false;
   }
   buffer = removeString(buffer, receiver);
   
   subject = getString(buffer); /* get subject */
   if(!verifyStringLength(subject, MAX_SUBJ)) {
      return false;
   }
   buffer = removeString(buffer, subject);
   
   message = buffer; /* get message */

   string receiverFolder = folder + "/" + receiver;

   /* checks if receiver already has a folder for their messages */
   try {
      /* receiver does not have a folder */
      if(!fs::exists(receiverFolder)) { 
         fs::create_directory(receiverFolder);
         ofstream outfile;
         string counterFile = receiverFolder + "/" + COUNTERFILENAME;
         outfile.open(counterFile.c_str());
         if(!outfile){
            cerr << "counterFile couldn't be opened" << endl;
            return false;
         }
         outfile << 0 << endl;
      }
   } catch (fs::filesystem_error& error) {
      cerr << error.what() << endl;
      return false;
   }

   /* save message in a file */
   ofstream outfile; 
   string newFile = ""; /* name of the new file in which the message will be stored */
   string fileNumber = ""; /* make sure to get the highest file number, this can be a problem when someone deletes a lot of messages */
   
   
   fileNumber += getHighestFileNumber(receiverFolder);
   if(strcasecmp(fileNumber.c_str(), "ERR") == 0) {
      return false;
   }
   newFile += receiverFolder + "/" + fileNumber + ".txt";
   
   /* write sender(\n)receiver(\n)subject(\n)message into file */
   outfile.open(newFile.c_str()); 
   if(!outfile){
      cerr << "counterFile couldn't be opened" << endl;
      return false;
   }
   outfile << sender << endl << receiver << endl << subject << endl << message;
   outfile.close(); 

   return true;
   
}

/**
 * @brief Responsible for LIST request from the client. Gets username from client request, opens username's directory,
 * iterates over messages in the directory and gets the subjects from the messages
 * 
 * @param buffer string received from client in the form of "l;username"
 * @param folder given directory where messages should be persisted
 * @return "ERR" if user/file not found, otherwise: "OK;numOfFiles;subject1;subject2;...;subjectn" 
 */
string list(string buffer, string folder)
{

   buffer = removeString(buffer, "l"); /* remove flag at the beginning of the package */

   string username = buffer; /* get username */
   if(!verifyStringLength(username, MAX_NAME)) {
      return "ERR";
   }
   string userFolder = folder + "/" + username; /* get username's folder */
   
   try {
      if(!fs::exists(userFolder)){ /* username doesn't have a folder -> return 0 */
        cout << userFolder << "does not exist" << endl;
         return to_string(0);
      }
   } catch (fs::filesystem_error& error) {
      cerr << error.what() << endl;
      return "ERR";
   }
   

   const fs::path path = userFolder; 
   string helperString; /* return string */

   helperString += to_string(getNumOfFiles(userFolder));

   try{
      for (const auto& entry : fs::directory_iterator(path)) {
         const auto filenameStr = entry.path().filename().string(); /* get name of file */
         if(strcasecmp(filenameStr.c_str(), COUNTERFILENAME) == 0) {
            /* ignore the file where the number of files is stored */
            continue;
         }
         helperString += SEPERATOR;
         
         /* open the file and get the subject */
         string line;
         string subject; 
         ifstream file(userFolder + "/" + filenameStr);
         int counter = 0;

         if(file.is_open()) {
            while(getline(file, line)){
               ++counter;
               if(counter == 3) { /* subject is in the third line of every file */
                  subject = line;
               }
            }
         } else {
            cerr << strerror(errno);
            cout << "file.is_open() error" << endl;
            return "ERR";
         }

         file.close();
         helperString += subject;

      }
   } catch (fs::filesystem_error& error) {
      cerr << error.what() << endl;
      return "ERR";
   }
   cout << "list: " << helperString << endl;
   return "OK;" + helperString;
}

/**
 * @brief Responsible for READ requests from client. Parses buffer string, opens messageNumber.txt in username's directory
 * and returns the message in messageNumber.txt
 * 
 * @param buffer string received from client in the form of "r;username;messageNumber"
 * @param folder given directory where messages should be persisted
 * @return string "error" if file with messageNumber.txt isn't in the users directory, otherwise returns message
 */
string read(string buffer, string folder)
{

   buffer = removeString(buffer, "r"); /* remove flag at the beginning of the package */

   string username, messageNumber;

   username = getString(buffer);
   if(!verifyStringLength(username, MAX_NAME)) {
      return "ERR";
   }
   buffer = removeString(buffer, username);

   messageNumber = buffer;
   string usernameFolder = folder + "/" + username;
   string searchedFileDirectory = usernameFolder + "/" + messageNumber + ".txt";

   try{
      if(!fs::exists(searchedFileDirectory)){
         cout << "file does not exist" << endl;
         return "ERR";
      }
   } catch (fs::filesystem_error& error) {
      cerr << error.what() << endl;
      return "ERR";
   }

   string message; /* message to return to client */
   string line; /* line buffer for file */

   ifstream file(searchedFileDirectory); /* copy entire content of searched File into message */
   if(file.is_open()) {
      for(int i=0; i<3; i++){
         getline(file, line);
         message+=line;
         message+=";";
      }
      while(!file.eof()) { 
         getline(file, line);
         message += line;
         message += "\n";
      }
   } else {
      cerr << "couldn't open file" << endl;
      return "ERR";
   }
   file.close();
   
   return "OK;" + message;

}

/**
 * @brief Responsible for the DEL request from the client. Searches for the file in the username's directory and deletes it
 * 
 * @param buffer string received from client in the form of "d;username;messageNumber"
 * @param folder given directory where messages should be persisted
 * @return true messageNumber.txt was found and deleted
 * @return false messageNumber.txt doesn't exist/ can't be deleted
 */
bool deleteMessage(string buffer, string folder)
{
   buffer = removeString(buffer, "d"); /* remove flag at the beginning of the package */

   string username, messageNumber;

   username = getString(buffer);
   if(!verifyStringLength(username, MAX_NAME)) {
      return false;
   }
   buffer = removeString(buffer, username);

   messageNumber = buffer;
   string usernameFolder = folder + "/" + username;
   string searchedFileDirectory = usernameFolder + "/" + messageNumber + ".txt";

   try {
      if(!fs::exists(searchedFileDirectory)) {
         return false;
      }

      fs::remove(searchedFileDirectory);
   } catch (fs::filesystem_error& error) {
      cerr << error.what() << endl;
      return false;
   }
   return true;
}

/**
 * @brief signal handler, safely closes all resources after SIGINT
 * 
 * @param sig SIGINT
 */
void signalHandler(int sig)
{
   if (sig == SIGINT) {
      printf("abort Requested... \n"); // ignore error
      abortRequested = 1;
      /* With shutdown() one can initiate normal TCP close sequence ignoring the reference count. */
      if (new_socket != -1) {
         if (shutdown(new_socket, SHUT_RDWR) == -1) {
            perror("shutdown new_socket");
         }
         if (close(new_socket) == -1) {
            perror("close new_socket");
         }
         new_socket = -1;
      }

      if (create_socket != -1) {
         if (shutdown(create_socket, SHUT_RDWR) == -1) {
            perror("shutdown create_socket");
         }
         if (close(create_socket) == -1) {
            perror("close create_socket");
         }
         create_socket = -1;
      }
   }
   else
   {
      exit(sig);
   }
}

/**
 * @brief prints correct usage of the program
 */
void printUsage(void)
{
    printf("Incorrect usage. Start the server using: \"./twmailer-server <port> <mail-spool-directoryname>\"\n");
    exit(EXIT_FAILURE);
}

/**
 * @brief Get the number of existent files in the given folder
 * 
 * @param folder user directory where files(messages) are stored
 * @return int - number of files in the given folder
 */
int getNumOfFiles(string folder)
{
   int count = 0;
   try {
      fs::path path = folder;

      for (auto& p : fs::directory_iterator(path)) {
         count++;
      }
   } catch (fs::filesystem_error& error) {
      cerr << error.what() << endl;
      exit(EXIT_FAILURE);
   }
   return count - 1; /* - 1 because numOfFiles.txt doesn't count to the number of messages */
}

/**
 * @brief Read the number of files that the user has received from numOfFiles.txt that is in the user's
 * directory. Then update the number to numOfFiles + 1 since a new message is being added and this
 * method is only called in the receive method. This way repeats due to deletes should be impossible.
 * 
 * @param folder user's directory where messages are stored
 * @return string numOfFiles if okay, ERR if something went wrong
 */
string getHighestFileNumber(string folder)
{
   int numOfFiles = 0;
   ifstream readFile; /* open numOfFile file to read from it */
   string counterFile = folder + "/" + COUNTERFILENAME;
   readFile.open(counterFile.c_str());
   if (!readFile){
      cerr << "readFile could not be opened" << endl;
      return "ERR";
   } else {
      while (1) {
			readFile >> numOfFiles;
			if (readFile.eof())
				break;

			cout << numOfFiles;
		}
      readFile.close();
   }

   ofstream outFile; /* write new highest number into outFile */
   numOfFiles++;
   outFile.open(counterFile.c_str());
   if (!outFile){
      cerr << "readFile could not be opened" << endl;
      return "ERR";
   } else {
      outFile << numOfFiles;
   }

   return to_string(numOfFiles);

}

/**
 * @brief Get the string until the delimiter ";"
 * 
 * @param buffer string with the form "%s1%;..."
 * @return string until the delimiter ";"
 */
string getString(string buffer)
{
   string helper;
   size_t pos = buffer.find(SEPERATOR);
   helper = buffer.substr(0, pos);
   return helper;
}

/**
 * @brief Used to update buffer and change it from the form "%s1%;%s2%" to the form "%s2%"
 * 
 * @param buffer string with the form "%s1%;%s2%"
 * @param s1 string with the form "%s1%;"
 * @return string buffer without "%s1%;"
 */
string removeString(string buffer, string s1)
{
   return buffer.erase(0, s1.length() + 1);
}

/**
 * @brief verify that the given string isn't longer than maxStringLength
 * @return true string is shorter than maxStringLength
 * @return false string is longer than maxStringLength
 */
bool verifyStringLength(string string, int maxStringLength)
{
   return (string.length() <= (unsigned)maxStringLength);
}

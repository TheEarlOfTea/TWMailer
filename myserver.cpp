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
#define PORT 6543
#define SEPERATOR ";"

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;
using namespace std;
namespace fs = std::filesystem;

///////////////////////////////////////////////////////////////////////////////

void *clientCommunication(void *data, string folder);
void receiveFromClient(string buffer, string folder);
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


///////////////////////////////////////////////////////////////////////////////


int main(int argc, char* argv[])
{
   /* ARGUMENT HANDLING */
   if(argc == 1 || argc >= 3) { /* TODO: change to four when I figure out this whole PORT thing */
      printUsage();
   }

   string folder = argv[1]; /* TODO: change to two when yk port thing */

   try {
      if (!fs::is_directory(folder)) { /* given persistence folder does not exist and needs to be created */
         cout << folder << " does not exist. Creating now..." << endl;
         fs::create_directory(folder);
      } 
   } catch(fs::filesystem_error error) {
      cerr << error.what() << endl;
      exit(EXIT_FAILURE);
   }

   /* END OF ARGUMENT HANDLING */
   


   socklen_t addrlen;
   struct sockaddr_in address, cliaddress;
   int reuseValue = 1;
   

   ////////////////////////////////////////////////////////////////////////////
   // SIGNAL HANDLER
   // SIGINT (Interrup: ctrl+c)
   // https://man7.org/linux/man-pages/man2/signal.2.html
   if (signal(SIGINT, signalHandler) == SIG_ERR) {
      perror("signal can not be registered");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as client)
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror("Socket error"); // errno set by socket()
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // SET SOCKET OPTIONS
   // https://man7.org/linux/man-pages/man2/setsockopt.2.html
   // https://man7.org/linux/man-pages/man7/socket.7.html
   // socket, level, optname, optvalue, optlen
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

   ////////////////////////////////////////////////////////////////////////////
   // INIT ADDRESS
   // Attention: network byte order => big endian
   memset(&address, 0, sizeof(address));
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons(PORT);

   ////////////////////////////////////////////////////////////////////////////
   // ASSIGN AN ADDRESS WITH PORT TO SOCKET
   if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
      perror("bind error");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // ALLOW CONNECTION ESTABLISHING
   // Socket, Backlog (= count of waiting connections allowed)
   if (listen(create_socket, 5) == -1) {
      perror("listen error");
      return EXIT_FAILURE;
   }

   while (!abortRequested)
   {
      /////////////////////////////////////////////////////////////////////////
      // ignore errors here... because only information message
      // https://linux.die.net/man/3/printf
      printf("Waiting for connections...\n");

      /////////////////////////////////////////////////////////////////////////
      // ACCEPTS CONNECTION SETUP
      // blocking, might have an accept-error on ctrl+c
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

      /////////////////////////////////////////////////////////////////////////
      // START CLIENT
      // ignore printf error handling
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

   ////////////////////////////////////////////////////////////////////////////
   // SEND welcome message
   strcpy(buffer, "Welcome to the server!\r\nPlease enter your commands...\r\n");
   if (send(*current_socket, buffer, strlen(buffer), 0) == -1) {
      perror("send failed");
      return NULL;
   }


   do
   {
      /////////////////////////////////////////////////////////////////////////
      // RECEIVE
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
      

      switch(flag){
         case 's': /* SEND */
               receiveFromClient(bufferString, folder);
            break;
         case 'l': /* LIST */
               list(bufferString, folder);
               //TODO: ADD SEND
            break;
         case 'r': /* READ */
               read(buffer, folder);
               //TODO: ADD SEND
            break;
         case 'd': /* DELETE */
               deleteMessage(buffer, folder);
               //TODO: ADD SEND
               //true: file deleted OK, false: file doesn't exist ERR
            break;
         case '?':
            perror("Invalid message");
            break;
         default: 
            break;
      }

      if (send(*current_socket, "OK", 3, 0) == -1)
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
 */
void receiveFromClient(string buffer, string folder){

   string sender, receiver, subject, message;
   
   buffer = removeString(buffer, "s"); /* remove flag at the beginning of the package */

   sender = getString(buffer); /* get sender */
   buffer = removeString(buffer, sender);
   
   receiver = getString(buffer); /* get receiver */
   buffer = removeString(buffer, receiver);
   
   subject = getString(buffer); /* get subject */
   buffer = removeString(buffer, subject);
   
   message = buffer; /*get message */

   /* checks if receiver already has a folder for their messages */
   try {
      string receiverFolder = folder + "/" + receiver;

      /* receiver does not have a folder */
      if(!fs::exists(receiverFolder)) { 
         fs::create_directory(receiverFolder);
      }
   } catch (fs::filesystem_error error) {
      cerr << error.what() << endl;
   }

   //TODO: ADD ERROR HANDLING FOR FILE

   /* save message in a file */
   ofstream outfile; 
   string newFile = ""; /* name of the new file in which the message will be stored */
   string fileNumber = ""; /* make sure to get the highest file number, this can be a problem when someone deletes a lot of messages */
   
   string receiverFolder = folder + "/" + receiver;
   fileNumber += getHighestFileNumber(receiverFolder);
   newFile += receiverFolder + "/" + fileNumber + ".txt";
   
   /* write into subject;message into file */
   outfile.open(newFile.c_str()); 
   outfile << subject << SEPERATOR << message;
   outfile.close(); 
   
}

/**
 * @brief Responsible for LIST request from the client. Gets username from client request, opens username's directory,
 * iterates over messages in the directory and gets the subjects from the messages
 * 
 * @param buffer string received from client in the form of "l;username"
 * @param folder given directory where messages should be persisted
 * @return string "0" if user/file not found, otherwise: "numOfFiles;subject1;subject2;...;subjectn" 
 */
string list(string buffer, string folder)
{
   
   buffer = removeString(buffer, "l"); /* remove flag at the beginning of the package */

   string username = buffer; /* get username */
   string userFolder = folder + "/" + username; /* get username's folder */
   
   if(!fs::exists(userFolder)){ /* username doesn't have a folder -> return 0 */
      return to_string(0);
   }

   const fs::path path = userFolder; 
   string helperString; /* return string */

   helperString += to_string(getNumOfFiles(userFolder));

   for (const auto& entry : fs::directory_iterator(path)) {
      const auto filenameStr = entry.path().filename().string(); /* get name of file */
      helperString += SEPERATOR;
      
      /* open the file and get the subject */

      string subject; 
      ifstream file(userFolder + "/" + filenameStr);

      if(file.is_open()) {
         getline(file, subject);
      } else {
         cerr << strerror(errno);
      }

      subject = getString(subject); 
      file.close();
      helperString += subject;

      //TODO: add file error catching

   }

   return helperString;
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
   buffer = removeString(buffer, username);

   messageNumber = buffer;
   string usernameFolder = folder + "/" + username;
   string searchedFileDirectory = usernameFolder + "/" + messageNumber + ".txt";

   if(!fs::exists(searchedFileDirectory)){
      //TODO: Change this, turn it into an exception instead
      return "error";
   }

   string message;
   string line;
   bool removedSubject = false; /* files have the form subject;message, remove subject to get the actual message */
   
   ifstream file(searchedFileDirectory);
   if(file.is_open()) {
      while(!file.eof()) { 
         getline(file, line);
         message += "\n";
         if(!removedSubject) {
            string subject = getString(line);
            message = "";
            message = removeString(line, subject);
            removedSubject = true;
            continue;
         }
         message += line;
      }
   } else {
      cerr << strerror(errno);
   }
   file.close();
   
   //TODO: add file error handling

   return message;

}

/**
 * @brief Responsible for the DEL request from the client. Searches for the file in the username's directory and deletes it
 * 
 * @param buffer string received from client in the form of "d;username;messageNumber"
 * @param folder given directory where messages should be persisted
 * @return true messageNumber.txt was found and deleted
 * @return false messageNumber.txt doesn't exist and can't be deleted
 */
bool deleteMessage(string buffer, string folder)
{
   buffer = removeString(buffer, "d"); /* remove flag at the beginning of the package */

   string username, messageNumber;

   username = getString(buffer);
   buffer = removeString(buffer, username);

   messageNumber = buffer;
   string usernameFolder = folder + "/" + username;
   string searchedFileDirectory = usernameFolder + "/" + messageNumber + ".txt";

   if(!fs::exists(searchedFileDirectory)) {
      return false;
   }

   fs::remove(searchedFileDirectory);
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
      /////////////////////////////////////////////////////////////////////////
      // With shutdown() one can initiate normal TCP close sequence ignoring
      // the reference count.
      // https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
      // https://linux.die.net/man/3/shutdown
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
    printf("Incorrect usage. Start the server using: \"./server <mail-spool-directoryname>\"\n");
    exit(EXIT_FAILURE);
}

/**
 * @brief Get the number of files in the given folder
 * 
 * @param folder user directory where files(messages) are stored
 * @return int - number of files in the given folder
 */
int getNumOfFiles(string folder)
{
   int count = 0;
   fs::path path = folder;

   for (auto& p : fs::directory_iterator(path)) {
      count++;
   }

   return count;
}

/**
 * @brief Finds the highest file number in the user's directory and returns highestFileNumber + 1 so that the server
 * knows what the next file should be called
 * 
 * @param folder user directory where files(messages) are stored
 * @return string highestFileNumber + 1
 */
string getHighestFileNumber(string folder)
{
   int currentNumber = 0;
   int highestNumber = 0;
   string returnString;
   fs::path path = folder;
   for (const auto& entry : fs::directory_iterator(path)) {
      const auto filenameStr = entry.path().filename().string();
      size_t pos = filenameStr.find('.');
      currentNumber = stoi(filenameStr.substr(0, pos));
      if(highestNumber < currentNumber) {
         highestNumber = currentNumber;
      }
   }

   return to_string(highestNumber + 1);

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

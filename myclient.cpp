#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
///////////////////////////////////////////////////////////////////////////////
#define PORT 6543

#define MAX_SUBJ 80;
#define BUF 1024;
#define MAX_NAME 8;
#define SEPERATOR ";";

#define CORRECT_SERVER_RESPONSE "OK";

using namespace std;

namespace auxilliary{
    string seperator= SEPERATOR;
    unsigned int max_name=MAX_NAME;
    unsigned int max_subj= MAX_SUBJ;
    unsigned int buffer= BUF;
    unsigned int max_msg= buffer - max_subj-(2*max_name)-1;

    bool isNameOk(string name){
        if(name.length()>max_name){
            cout<<"Given name is to long\n";
            return false;
        }
        if(name.length()<=0){
            cout<<"Entered empty name\n";
            return false;
        }
        for(char c : name){
            if (!std::isalpha(c) && !std::isdigit(c)){
                cout<<"Given name contains illegal characters\n";
                return false;
            }
        }
        return true;

    }
    bool isSubjectOk(string subject){
        if(subject.length()> max_subj){
            printf("Subject may only be up to %i characters long;\nInput Subject-length was: %li\n", max_subj, subject.length());
            return false;
        }
        return true;
    }
    
    bool isMessageOk(string message){
        if(message.length()> max_msg){
            printf("Message may only be up to %i characters long;\nInput Message-length was: %li\n", max_msg, message.length());
            return false;
        }
        return true;
    }
    
    bool isNumberOk(string number){
        if(number.length()<=0){
            cout<<"No number was put in\n";
            return false;
        }
        for(char c : number){
            if (!std::isdigit(c)){
                cout<<"Given String contains non-digit characters\n";
                return false;
            }
        }
        if(stoi(number)<=0){
            cout<<"Input Number must be higher than 0\n";
            return false;
        }
        return true;
    }
}

namespace client_functions{
    using namespace auxilliary;

    string send(){
        string sender, receiver, subject, message, hs, package;
        do{
            cout<<"<Sender>: ";
            getline(cin,sender);

        }while(!isNameOk(sender));
        do{
            cout<<"<Receiver>: ";
            getline(cin,receiver);
        }while(!isNameOk(receiver));
        do{
            cout<<"<Subject>: ";
            getline(cin,subject);
        }while(!isSubjectOk(subject));
        do{
            cout<<"<Message>:\n";
            do{
                hs="";
                getline(cin, hs);
                if(hs!="."){
                    message+=hs;
                    message+="\n";
                }
            }while(hs!=".");
        }while(!isMessageOk(message));

        package="s"+seperator+sender+seperator+receiver+seperator+subject+seperator+message;
        return package;
    }
    
    string list(){
        string username, package;
        do{
            cout<<"<Username>: ";
            getline(cin,username);
        }while(!isNameOk(username));

        package="l"+seperator+username;
        return package;
    }

    string read(){
        string username, package, msg_number;
        do{
            cout<<"<Username>: ";
            getline(cin, username);
        }while(!isNameOk(username));
        do{
            cout<<"<Message-Number>: ";
            getline(cin, msg_number);
        }while(!isNumberOk(msg_number));

        package="r"+seperator+username+seperator+msg_number;
        return package;

    }

    string del(){
        string username, package, msg_number;
        do{
            cout<<"<Username>: ";
            getline(cin, username);
        }while(!isNameOk(username));
        do{
            cout<<"<Message-Number>: ";
            getline(cin, msg_number);
        }while(!isNumberOk(msg_number));
        
        package="d"+seperator+username+seperator+msg_number;
        return package;


    }

}
using namespace client_functions;

int main(int argc, char **argv){

    const size_t BUFFER_SIZE= BUF;
    int create_socket;
    char buffer[buffer];
    struct sockaddr_in address;
    int size;
    bool isQuit, isEntryCorrect;
    string hs;

    if((create_socket= socket(AF_INET, SOCK_STREAM, 0))==-1){
        perror("Fehler beim erstellen des Sockets");
        exit(EXIT_FAILURE);
    }

    memset(&address, 0, sizeof(address));
    address.sin_family= AF_INET;

    if (argc < 2){

        inet_aton("127.0.0.1", &address.sin_addr);
        address.sin_port= htons(PORT);
    }
    else{
        inet_aton(argv[1], &address.sin_addr);
        //TODO Port conversion
    }

    if(connect(create_socket, (struct sockaddr *)&address, sizeof(address))==-1){
        perror("Server konnte nicht erreicht werden");
        exit(EXIT_FAILURE);
    }

    printf("Connection with server (%s) established\n",
        inet_ntoa(address.sin_addr));

    size= recv(create_socket, buffer, BUFFER_SIZE-1, 0);
    switch(size){
        case -1:
            perror("recv error");
            break;
        case 0:
            printf("Server closed remote socket\n");
            break;
        default:
            buffer[size] = '\0';
            printf("%s", buffer); // ignore error
    }

    do{
        printf(">> ");
        if (fgets(buffer, BUFFER_SIZE, stdin) != NULL){
            int size = strlen(buffer);
            // remove new-line signs from string at the end
            if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n'){
                size -= 2;
                buffer[size] = 0;
            }
            else if (buffer[size - 1] == '\n'){
                --size;
                buffer[size] = 0;
            }

            if(strcasecmp(buffer,"send")==0){
                hs=send();
                isEntryCorrect=true;
            }
            else if(strcasecmp(buffer,"list")==0){
                hs=list();
                isEntryCorrect=true;
            }
            else if(strcasecmp(buffer,"read")==0){
                hs=read();
                isEntryCorrect=true;
            }
            else if(strcasecmp(buffer,"del")==0){
                hs=del();
                isEntryCorrect=true;
            }
            else{
                isEntryCorrect=false;
            }


            
            isQuit = strcmp(buffer, "quit") == 0;
            if(isEntryCorrect){
                
                strcpy(buffer,hs.c_str());
                size=strlen(buffer);
                if(send(create_socket, buffer, size, 0)==-1){

                    perror("An error weil sending data occured");
                    break;

                }
                size = recv(create_socket, buffer, BUFFER_SIZE-1, 0);
                if (size == -1){
                    perror("recv error");
                    break;
                }
                else if (size == 0){
                    printf("Server closed remote socket\n"); // ignore error
                    break;
                }
                else{
                    buffer[size] = '\0';
                    printf("<< %s\n", buffer); // ignore error
                    if (strcmp("OK", buffer) != 0){
                    fprintf(stderr, "<< Server error occured, abort\n");
                    break;
                    }
                }
            }         
        }
    }while (!isQuit);
    if (create_socket != -1){
      if (shutdown(create_socket, SHUT_RDWR) == -1){

         perror("shutdown create_socket"); 
      }
      if (close(create_socket) == -1){

         perror("close create_socket");
      }
      create_socket = -1;
   }

   return EXIT_SUCCESS;

    
}
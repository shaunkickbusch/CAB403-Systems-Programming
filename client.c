#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>

#define MESSAGE_LENGTH 1024
volatile sig_atomic_t exitProgramFlag = 0;
int clientSocket;

void catchCtrlC(int sig) {
    exitProgramFlag = 1;
}

void sendMessageFunction() {

    char message[MESSAGE_LENGTH] = {};
	char buffer[MESSAGE_LENGTH + 32] = {};

    while(1){
  	    printf("%s", "> ");
        fflush(stdout);
        fgets(message, MESSAGE_LENGTH, stdin);
        /*replace new line with string NULL*/
        for (int i = 0; i < MESSAGE_LENGTH; i++) {
            if (message[i] == '\n') {
                message[i] = '\0';
                break;
            }
        }

        if(strcmp(message, "BYE") == 0){
		    break;
        }
        else{
            sprintf(buffer, "%s", message);
            send(clientSocket, buffer, strlen(buffer), 0);
        }
		bzero(message, MESSAGE_LENGTH);
        bzero(buffer, MESSAGE_LENGTH + 32);
    }
    catchCtrlC(2);
}

void receiveMessageFunction(){
    char message[MESSAGE_LENGTH] = {};
    while (1){
		int receiveData = recv(clientSocket, message, MESSAGE_LENGTH, 0);
        if (receiveData > 0){
            printf("%s\n", message);
            printf("%s", "> ");
            fflush(stdout);
        } 
        else if (receiveData == 0){
			break;
        }
        /*Clear the buffer*/
        bzero(message, sizeof(message));
    }
}

int main(int argc, char **argv){

	struct sockaddr_in serverAddr;
    pthread_t sendMessage;
    pthread_t receiveMessage;
    int connectNum;
    signal(SIGINT, catchCtrlC);
  
    if(argc == 1){
        fprintf(stderr, "No IP Address or Socket Entered. Please follow the ./client <server_IP_address> <sock_number> format.\n");
        exit(1);
    }
    else if(argc == 2){
        fprintf(stderr, "No Socket Number Entered. Please follow the ./client <server_IP_address> <sock_number> format.\n");
        exit(1);
    }
    else if(argc != 3){
        fprintf(stderr, "Please follow the ./client <server_IP_address> <sock_number> format.\n");
        exit(1);
    }
    
	clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        fprintf(stderr, "Failed to create socket\n");
        exit(1);
    }
    printf("Successfully created client socket.\n");
    

	bzero(&serverAddr, sizeof(serverAddr));  
	serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(argv[1]);
    serverAddr.sin_port = htons(atoi(argv[2]));

    connectNum = connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
	if (connectNum == -1) {
        fprintf(stderr, "Failed to connect\n");
        exit(1);
    }

    if(pthread_create(&sendMessage, NULL, (void *) sendMessageFunction, NULL) == -1){
        printf("Failed to create send message thread.\n");
        exit(1);
    }

    if(pthread_create(&receiveMessage, NULL, (void *) receiveMessageFunction, NULL) != 0){
		printf("Failed to create receive message thread.\n");
		exit(1);
	}

    while (1){
		if(exitProgramFlag){
		    printf("\nYou disconnected successfully.\n");
			break;
        }
	}
    close(clientSocket);
}
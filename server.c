#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define MAX_NUMBER_OF_CHANNELS 256
#define DEFAULT_PORT 12345
#define MAX_NUM_OF_MESSAGES_IN_CHANNEL 1000
#define MAX_CHANNEL_MESSAGE_SIZE 1024

static _Atomic unsigned int connectedClientCount = 0;
volatile sig_atomic_t exitFlag = 0;
static int clientIDAssigner = 0;
int exitLivefeed = 0;

/*Client*/
typedef struct{
	struct sockaddr_in address;
    int subbedChannels[MAX_NUMBER_OF_CHANNELS];
	int clientSocket;
	int clientID;
} myClient;

/*Channel*/
typedef struct{
    int channelIDForMessage[MAX_NUM_OF_MESSAGES_IN_CHANNEL];
    char channelMessage[MAX_NUM_OF_MESSAGES_IN_CHANNEL][MAX_CHANNEL_MESSAGE_SIZE];
    int readMessages[MAX_CLIENTS][MAX_NUM_OF_MESSAGES_IN_CHANNEL];
    int amountOfMessageSent;
} messageBank;

myClient *clients[MAX_CLIENTS];

pthread_mutex_t clientsMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t readAndWriteMutex = PTHREAD_MUTEX_INITIALIZER;

/*Send the message from the server to only the current client it's communicating with*/
void sendMessage(char *myBuffer, int currentClientID){

	pthread_mutex_lock(&clientsMutex);
	for(int i = 0; i < MAX_CLIENTS; ++i){
        /*If the client exists*/
		if(clients[i]){
            /*If the client is our current client*/
			if(clients[i]->clientID == currentClientID){
                /*Write the message our current client's socket and send the message*/
				if(write(clients[i]->clientSocket, myBuffer, strlen(myBuffer)) == -1){
					printf("Failed to write to the buffer.");
					break;
				}
			}
		}
	}
	pthread_mutex_unlock(&clientsMutex);
}

bool isValidChannel(char *text){
    char *endptr;
    int result = strtol(text, &endptr, 10);
    if(*endptr != '\0' || (result < 0 || result > 255)){
        return false;
    }
    else{
        return true;
    }
}

void *clientFunction(void *clientPointer, messageBank *myMessageBank){
	char buff_out[BUFFER_SZ];
	int leave_flag = 0;
    connectedClientCount++;
	myClient *thisClient = (myClient *)clientPointer;
    bool validCommand = true;

	sprintf(buff_out, "Welcome! Your client ID is %d.", thisClient->clientID);
	printf("Received a connection from client %d.\n", thisClient->clientID);
	sendMessage(buff_out, thisClient->clientID);
	bzero(buff_out, BUFFER_SZ);

	while(1){
		if (leave_flag == 1) {
			break;
		}

		int receive = recv(thisClient->clientSocket, buff_out, BUFFER_SZ, 0);
        /*If we've received new data*/
		if (receive > 0){
            /*If the buffer isn't empty*/
			if(strlen(buff_out) > 0){
                char *original, *token;
                original = strdup(buff_out);
                token = strtok(original," ");

                if(strcmp(buff_out, "BYE") == 0){
                    printf("Client %d has disconnected.\n", thisClient->clientID);
                    /*Insert message into the buffer*/
                    sprintf(buff_out, "You've disconnected successfully.");
                    sendMessage(buff_out, thisClient->clientID);
				    printf("Sent the following message to client %d: %s\n", thisClient->clientID, buff_out);
                    bzero(buff_out, BUFFER_SZ);
			        leave_flag = 1;
                }

                else if(strcmp(token, "SUB") == 0){
                    /*Increment the token to the next characters*/
                    token = strtok(NULL, " ");
                    /*If there's nothing present after SUB we break*/
                    if(token == NULL){
                        validCommand = false;
                    }
                    else{
                        if(isValidChannel(token) == false){
                            printf("Client %d tried to subscribe to an invalid channel.\n", thisClient->clientID);
                            sprintf(buff_out, "Invalid channel: %s.", token);
                        }
                        else if(isValidChannel(token) == true){
                            int channelNumber = atoi(token);
                            if(thisClient->subbedChannels[channelNumber] == 0){
                                printf("Client %d has been successfully subbed to channel %d.\n", thisClient->clientID, channelNumber);
                                sprintf(buff_out, "Subscribed to channel %d.", channelNumber);
                                thisClient->subbedChannels[channelNumber] = 1;
                            }
                            else if(thisClient->subbedChannels[channelNumber] == 1){
                                printf("Client %d is already subbed to channel %d.\n", thisClient->clientID, channelNumber);
                                sprintf(buff_out, "Already subscribed to channel %d.", channelNumber);
                            }
                        }
                        else{
                            printf("Unhandled exception");
                            sprintf(buff_out, "Unhandled exception");
                        }
                    }
                }
            
                else if(strcmp(token, "UNSUB") == 0){
                    /*Increment the token to the next characters*/
                    token = strtok(NULL, " ");
                    if(token == NULL){
                        validCommand = false;
                    }
                    else{
                        if(isValidChannel(token) == false){
                            printf("Client %d tried to unsubscribe from an invalid channel.\n", thisClient->clientID);
                            sprintf(buff_out, "Invalid channel: %s.", token);
                        }
                        else if(isValidChannel(token) == true){
                            int channelNumber = atoi(token);
                            if(thisClient->subbedChannels[channelNumber] == 1){
                                printf("Client %d has been successfully unsubscribed from channel %d.\n", thisClient->clientID, channelNumber);
                                sprintf(buff_out, "Unsubscribed from channel %d.", channelNumber);
                                thisClient->subbedChannels[channelNumber] = 0;
                            }
                            else if(thisClient->subbedChannels[channelNumber] == 0){
                                printf("Client %d is already subbed to channel %d.\n", thisClient->clientID, channelNumber);
                                sprintf(buff_out, "Not subscribed to channel %d.", channelNumber);
                            }
                            else{
                                printf("Unexpected error trying to unsubscribe client %d from channel %d\n", thisClient->clientID, channelNumber);
                                sprintf(buff_out, "Unexpexted error trying to unsub from channel %d.", channelNumber);
                            }
                        }
                        else{
                            printf("Unhandled exception");
                            sprintf(buff_out, "Unhandled exception");
                        }
                    }
                }
                /*Check if user sent the send command*/
                else if(strcmp(token, "SEND") == 0){
                    /*Increment the token to the next characters*/
                    token = strtok(NULL, " ");
                    /*If there's not another string after SEND we break*/
                    if(token == NULL){
                        validCommand = false;
                    }
                    else{
                        if(isValidChannel(token) == false){
                            printf("Invalid channel: %s.\n", token);
                            sprintf(buff_out, "Invalid channel: %s.", token);
                        }
                        else if(isValidChannel(token) == true){
                            int channelNumber = atoi(token);
                            token = strtok(NULL, " ");
                            /*Checks if we have a message following the channel number, if not then break*/
                            if(token == NULL){
                                validCommand = false;
                            }
                            else{
                                pthread_mutex_lock(&readAndWriteMutex);
                                /*Add the message to the channel*/
                                strcpy(myMessageBank[0].channelMessage[myMessageBank[0].amountOfMessageSent], token);
                                /*Associate that message with a channel number into the bank*/
                                myMessageBank[0].channelIDForMessage[myMessageBank[0].amountOfMessageSent] = channelNumber;
                                /*Mark a new message for all the clients*/
                                for(int i = 0; i < MAX_CLIENTS; i++){
                                    myMessageBank[0].readMessages[i][myMessageBank[0].amountOfMessageSent] = 0;
                                }
                                printf("The following message was added to channel %d: %s\n", myMessageBank[0].channelIDForMessage[myMessageBank[0].amountOfMessageSent], myMessageBank[0].channelMessage[myMessageBank[0].amountOfMessageSent]);
                                sprintf(buff_out, "Sent message successfully.");
                                /*Increment the amount of message sent ready for the next message*/
                                myMessageBank[0].amountOfMessageSent = myMessageBank[0].amountOfMessageSent + 1;
                                pthread_mutex_unlock(&readAndWriteMutex);
                            }
                        }
                        else{
                            printf("Unhandled exception");
                            sprintf(buff_out, "Unhandled exception");
                        }
                    }
                }
                else if(strcmp(token, "CHANNELS") == 0){
                    int messagesTotal = 0;
                    int messagesRead = 0;
                    int messagesUnread = 0;
                    /*Clear out the command the user sent us out of the buffer*/
                    bzero(buff_out, BUFFER_SZ);
                    for(int i = 0; i < MAX_NUMBER_OF_CHANNELS; i++){
                        if(thisClient->subbedChannels[i] == 1){
                            /*Add each channel the user is subbed to to the buffer*/
                            for(int x = 0; x < MAX_NUM_OF_MESSAGES_IN_CHANNEL; x++){
                                /*If a message has a channels ID there's a message*/
                                if(myMessageBank[0].channelIDForMessage[x] == i){
                                    messagesTotal++;
                                    if(myMessageBank[0].readMessages[thisClient->clientID][x] == 1){
                                        messagesRead++;
                                    }
                                    if(myMessageBank[0].readMessages[thisClient->clientID][x] == 0){
                                        messagesUnread++;
                                    }
                                }
                            }
                            sprintf(buff_out + strlen(buff_out), "\nChannel ID: %d\tMessages Count: %d\tMessages Read: %d\tMessages Not Read: %d", i, messagesTotal, messagesRead, messagesUnread);
                            messagesTotal = 0;
                            messagesRead = 0;
                            messagesUnread = 0;
                        }
                    }
                }
                /*Version of NEXT without channel ID*/
                else if(strcmp(buff_out, "NEXT") == 0){
                    pthread_mutex_lock(&readAndWriteMutex);
                    bool messageFound = false;
                    bool notSubbedToAnyChannels = true;
                    for(int i = 0; i < MAX_NUMBER_OF_CHANNELS; i++){
                        if(thisClient->subbedChannels[i] == 1){
                            notSubbedToAnyChannels = false;
                            for(int x = 0; x < MAX_NUM_OF_MESSAGES_IN_CHANNEL; x++){
                                /*If the message is unread*/
                                if(myMessageBank[0].readMessages[thisClient->clientID][x] == 0){
                                    sprintf(buff_out, "%d:%s", myMessageBank[0].channelIDForMessage[x], myMessageBank[0].channelMessage[x]);
                                    /*Marks the message as read*/
                                    myMessageBank[0].readMessages[thisClient->clientID][x] = 1;
                                    messageFound = true;
                                    break;
                                }
                            }
                            if(messageFound == true){
                                break;
                            }
                        }
                    }
                    /*User is up to date send nothing*/
                    if(messageFound == false && notSubbedToAnyChannels == false){
                        sprintf(buff_out, " ");
                    }
                    else if(messageFound == false && notSubbedToAnyChannels == true){
                        sprintf(buff_out, "Not subscribed to any channels.");
                        printf("Sent the following message to client %d: %s\n", thisClient->clientID, buff_out);
                    }
                    pthread_mutex_unlock(&readAndWriteMutex);
                }
                /*Version of NEXT with channel ID*/
                else if(strcmp(token, "NEXT") == 0){
                    pthread_mutex_lock(&readAndWriteMutex);
                    /*Increment the token to the next characters*/
                    token = strtok(NULL, " ");
                    /*If there's not another string after NEXT we break*/
                    if(token == NULL){
                        validCommand = false;
                    }
                    else{
                        if(isValidChannel(token) == false){
                            printf("Invalid channel: %s.\n", token);
                            sprintf(buff_out, "Invalid channel: %s.", token);
                        }
                        else if(isValidChannel(token) == true){

                            int channelNumber = atoi(token);
                            bool foundMessage = false;
                            /*If the client is subbed*/
                            if(thisClient->subbedChannels[channelNumber] == 1){
                                for(int i = 0; i < MAX_NUM_OF_MESSAGES_IN_CHANNEL; i++){
                                    /*If the message from the bank was sent to the channel the client entered*/
                                    if(myMessageBank[0].channelIDForMessage[i] == channelNumber){
                                        /*If the message is unread*/
                                        
                                        if(myMessageBank[0].readMessages[thisClient->clientID][i] == 0){
                                            sprintf(buff_out, "%s", myMessageBank[0].channelMessage[i]);
                                            /*Mark the message as read*/
                                            myMessageBank[0].readMessages[thisClient->clientID][i] = 1;
                                            foundMessage = true;
                                        }
                                    }
                                }
                            }
                            /*If the client is not subbed*/
                            else if(thisClient->subbedChannels[channelNumber] == 0){
                                printf("Client %d can't use NEXT command because he isn't subbed to channel %d.\n", thisClient->clientID, channelNumber);
                                sprintf(buff_out, "Not subscribed to channel %d", channelNumber);
                            }
                            else{
                                printf("Unhandled exception\n");
                                sprintf(buff_out, "Unhandled exception\n");
                            }
                            if(thisClient->subbedChannels[channelNumber] == 1 && foundMessage == false){
                                printf("No new messages for client %d from channel %d", thisClient->clientID, channelNumber);
                                sprintf(buff_out, " ");
                            }
                        }
                        else{
                            printf("Unhandled exception");
                            sprintf(buff_out, "Unhandled exception");
                        }
                    }
                    pthread_mutex_unlock(&readAndWriteMutex);
                }
                else if(strcmp(buff_out, "LIVEFEED") == 0){
                    pthread_mutex_lock(&readAndWriteMutex);
                    while(1){
                        for(int i = 0; i < MAX_NUMBER_OF_CHANNELS; i++){
                            if(thisClient->subbedChannels[i] == 1){
                                for(int x = 0; x < MAX_NUM_OF_MESSAGES_IN_CHANNEL; x++){
                                    /*If the message is unread*/
                                    if(myMessageBank[0].readMessages[thisClient->clientID][x] == 0){
                                        sprintf(buff_out, "%d:%s\n", myMessageBank[0].channelIDForMessage[x], myMessageBank[0].channelMessage[x]);
                                        /*Marks the message as read*/
                                        myMessageBank[0].readMessages[thisClient->clientID][x] = 1;
                                        sendMessage(buff_out, thisClient->clientID);
                                        bzero(buff_out, BUFFER_SZ);
                                    }
                                }
                            }
                        }
                    }
                    pthread_mutex_unlock(&readAndWriteMutex);
                }
                else if(strcmp(token, "LIVEFEED") == 0){
                    
                    /*Increment the token to the next characters*/
                    token = strtok(NULL, " ");
                    /*If there's not another string after NEXT we break*/
                    if(token == NULL){
                        validCommand = false;
                    }
                    else{
                        if(isValidChannel(token) == false){
                            printf("Invalid channel: %s.\n", token);
                            sprintf(buff_out, "Invalid channel: %s.", token);
                        }
                        else if(isValidChannel(token) == true){
                            int channelNumber = atoi(token);
                            if(thisClient->subbedChannels[channelNumber] == 1){
                                pthread_mutex_lock(&readAndWriteMutex);
                                while(1){
                                    for(int x = 0; x < MAX_NUM_OF_MESSAGES_IN_CHANNEL; x++){
                                        /*If the message is unread*/
                                        if(myMessageBank[0].readMessages[thisClient->clientID][x] == 0){
                                            sprintf(buff_out, "%d:%s\n", myMessageBank[0].channelIDForMessage[x], myMessageBank[0].channelMessage[x]);
                                            /*Marks the message as read*/
                                            myMessageBank[0].readMessages[thisClient->clientID][x] = 1;
                                            sendMessage(buff_out, thisClient->clientID);
                                            bzero(buff_out, BUFFER_SZ);
                                        }
                                    }
                                }
                                pthread_mutex_unlock(&readAndWriteMutex);
                            }
                        }
                    }
                }

                else{
                    printf("Received an invalid command from client %d: %s.\n", thisClient->clientID, buff_out);
                    sprintf(buff_out, "You entered an invalid command");
                }

                if(validCommand == false){
                    printf("Received an invalid command from client %d: %s.\n", thisClient->clientID, buff_out);
                    sprintf(buff_out, "You entered an invalid command");
                    validCommand = true;
                }

				sendMessage(buff_out, thisClient->clientID);
				//str_trim_lf(buff_out, strlen(buff_out));
				printf("Sent the following message to client %d: %s\n", thisClient->clientID, buff_out);
			}
		}
        else if (receive == 0){
			printf("Client %d has disconnected\n", thisClient->clientID);
			leave_flag = 1;
		} 
        else{
			printf("ERROR: -1\n");
			leave_flag = 1;
		}
        /*Clear the buffer*/
		bzero(buff_out, BUFFER_SZ);
	}

    /*Close the clients socket*/
	close(thisClient->clientSocket);
    /*Remove the client from the clients array*/
    pthread_mutex_lock(&clientsMutex);
	for(int i = 0; i < MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->clientID == clientIDAssigner){
				clients[i] = NULL;
				break;
			}
		}
	}
	pthread_mutex_unlock(&clientsMutex);
    free(thisClient);
    connectedClientCount--;
}

int main(int argc, char **argv){
    bool defaultPort = true;
    /*A port was entered as an initial argument*/
    if(argc == 2){
        char *endptr;
        int result = strtol(argv[1], &endptr, 10);
        /*An invalid port was entered as an initial argument*/
        if (*endptr != '\0') {
            fprintf(stderr, "Invalid port entered entered as argument.\n");
            exit(1);
        }
        else{
            defaultPort = false;
        }
    }

	int option = 1;
	int listenfd = 0;
    int cliSocket = 0;
    struct sockaddr_in serverAddress;
    struct sockaddr_in clientAddress;

    int portUsed;
    pid_t childpid;
    signal(SIGPIPE, SIG_IGN);

    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        fprintf(stderr, "Failed to create listen socket.\n");
        close(listenfd);
        exit(1);
    }
    printf("Listen socket created successfully...\n");
    memset(&serverAddress, '\0', sizeof(serverAddress));

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    if(defaultPort == false){
        serverAddress.sin_port = htons(atoi(argv[1]));
        portUsed = atoi(argv[1]);
    }
    else{
        serverAddress.sin_port = htons(DEFAULT_PORT);
        portUsed = DEFAULT_PORT;
    }

    if(bind(listenfd, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        fprintf(stderr, "Failed to bind.\n");
        close(listenfd);
        exit(1);
    }
    printf("Successfully bound to port %d...\n", portUsed);

    if (listen(listenfd, 10) < 0) {
        fprintf(stderr, "Failed to listen on socket.\n");
        close(listenfd);
        exit(1);
	}
    printf("Successfully listening on port %d...\n", portUsed);

    int messageBankSize = sizeof(messageBank);
    messageBank *messageBankPtr;
    messageBankPtr = mmap(NULL, messageBankSize, PROT_WRITE | PROT_READ, MAP_SHARED| MAP_ANONYMOUS, -1, 0);

    for(int i = 0; i < MAX_NUM_OF_MESSAGES_IN_CHANNEL; i++){
        messageBankPtr[0].channelIDForMessage[i] = -1;
    }
    messageBankPtr[0].amountOfMessageSent = 0;
    for(int i = 0; i < MAX_CLIENTS; i++){
        for(int x = 0; x < MAX_NUM_OF_MESSAGES_IN_CHANNEL; x++){
            messageBankPtr[0].readMessages[i][x] = -1;
        }
    }

    printf("Shared memory successfully initialised...\n");
    
    printf("Server is running...\n");


	while(1){
        socklen_t clilen = sizeof(clientAddress);
		cliSocket = accept(listenfd, (struct sockaddr*)&clientAddress, &clilen);

		/* Client settings */
		myClient *clientPointer = (myClient *)malloc(sizeof(myClient));
		clientPointer->address = clientAddress;
		clientPointer->clientSocket = cliSocket;
		clientPointer->clientID = clientIDAssigner++;

        for(int i = 0; i < MAX_NUMBER_OF_CHANNELS; i++){
            clientPointer->subbedChannels[i] = 0;
        }

		/*Add client pointer to the clients pointer array*/
        pthread_mutex_lock(&clientsMutex);
	    for(int i = 0; i < MAX_CLIENTS; ++i){
            /*If this client is present then add it to the pointer array*/
		    if(!clients[i]){
			    clients[i] = clientPointer;
			    break;
		    }
	    }
	    pthread_mutex_unlock(&clientsMutex);

        childpid = fork();
        if(childpid < 0){
            printf("Creating the fork failed");
        }
        else if(childpid == 0){

            clientFunction((void*)clientPointer, messageBankPtr);
        }
        else{
            
        }
	}
    
	return 0;
}
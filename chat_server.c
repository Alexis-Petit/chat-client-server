#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS	100
#define LENGTH_SEND 201
#define LENGTH_SEND_ALL 2048


static unsigned int cli_count = 0;
static int id = 10;

/* Client structure */
typedef struct {
	struct sockaddr_in addr;	/* Client remote address */
	int connfd;					/* Connection file descriptor */
	int id;						/* Client unique identifier */
	char name[32];				/* Client name */
} client_struct;

client_struct *clients[MAX_CLIENTS];

/* Add client to queue */
void add_queue(client_struct *cl){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(!clients[i]){
			clients[i] = cl;
			return;
		}
	}
}

/* Delete client from queue */
void delete_cli_from_queue(int id){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			if(clients[i]->id == id){
				clients[i] = NULL;
				return;
			}
		}
	}
}

/* Send message to all clients but the sender */
void send_message(char *s, int id){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			if(clients[i]->id != id){
				if(write(clients[i]->connfd, s, LENGTH_SEND)<0){
					perror("write");
					exit(-1);
				}
			}
		}
	}
}

/* Send message to all clients */
void send_message_all(char *s){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			if(write(clients[i]->connfd, s, LENGTH_SEND_ALL)<0){
				perror("write");
				exit(-1);
			}
		}
	}
}

/* Send message to sender */
void send_message_self(const char *s, int connfd){
	if(write(connfd, s, strlen(s))<0){
		perror("write");
		exit(-1);
	}
}

/* Send message to client */
void send_message_client(char *s, int id){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			if(clients[i]->id == id){
				if(write(clients[i]->connfd, s, strlen(s))<0){
					perror("write");
					exit(-1);
				}
			}
		}
	}
}

/* Send list of active clients */
void send_active_clients(int connfd){
	int i;
	char s[64];
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			sprintf(s, "<<CLIENT %d | %s\r\n", clients[i]->id, clients[i]->name);
			send_message_self(s, connfd);
		}
	}
}

/* Strip CRLF */
void strip_newline(char *s){
	while(*s != '\0'){
		if(*s == '\r' || *s == '\n'){
			*s = '\0';
		}
		s++;
	}
}

/* Print ip address */
void print_client_addr(struct sockaddr_in addr){
	printf("%d.%d.%d.%d",
		addr.sin_addr.s_addr & 0xFF,
		(addr.sin_addr.s_addr & 0xFF00)>>8,
		(addr.sin_addr.s_addr & 0xFF0000)>>16,
		(addr.sin_addr.s_addr & 0xFF000000)>>24);
}

/* Handle all communication with the client */
void *handle_client(void *arg){
	char buff_out[2048];
	char buff_in[1024];
	int rlen;

	cli_count++;
	client_struct *client= (client_struct *)arg;

	printf("<<ACCEPT ");
	print_client_addr(client->addr);
	printf(" REFERENCED BY %d\n", client->id);

	sprintf(buff_out, "<<JOIN, HELLO %s\r\n", client->name);
	send_message_all(buff_out);

	/* Receive input from client */
	while((rlen = read(client->connfd, buff_in, sizeof(buff_in)-1)) > 0){
	        buff_in[rlen] = '\0';
	        buff_out[0] = '\0';
		strip_newline(buff_in);

		/* Ignore empty buffer */
		if(!strlen(buff_in)){
			continue;
		}

		/* Special options */
		if(buff_in[0] == '\\'){
			char *command, *param;
			command = strtok(buff_in," ");
			if(!strcmp(command, "\\QUIT")){
				break;
			}else if(!strcmp(command, "\\PING")){
				send_message_self("<<PONG\r\n", client->connfd);
			}else if(!strcmp(command, "\\NAME")){
				param = strtok(NULL, " ");
				if(param){
					char *old_name = strdup(client->name);
					strcpy(client->name, param);
					sprintf(buff_out, "<<RENAME, %s TO %s\r\n", old_name, client->name);
					free(old_name);
					send_message_all(buff_out);
				}else{
					send_message_self("<<NAME CANNOT BE NULL\r\n", client->connfd);
				}
			}else if(!strcmp(command, "\\PRIVATE")){
				param = strtok(NULL, " ");
				if(param){
					int id = atoi(param);
					param = strtok(NULL, " ");
					if(param){
						sprintf(buff_out, "[PM][%s]", client->name);
						while(param != NULL){
							strcat(buff_out, " ");
							strcat(buff_out, param);
							param = strtok(NULL, " ");
						}
						strcat(buff_out, "\r\n");
						send_message_client(buff_out, id);
					}else{
						send_message_self("<<MESSAGE CANNOT BE NULL\r\n", client->connfd);
					}
				}else{
					send_message_self("<<REFERENCE CANNOT BE NULL\r\n", client->connfd);
				}
			}else if(!strcmp(command, "\\ACTIVE")){
				sprintf(buff_out, "<<CLIENTS %d\r\n", cli_count);
				send_message_self(buff_out, client->connfd);
				send_active_clients(client->connfd);
			}else if(!strcmp(command, "\\HELP")){
				strcat(buff_out, "\\QUIT     Quit chatroom\r\n");
				strcat(buff_out, "\\PING     Server test\r\n");
				strcat(buff_out, "\\NAME     <name> Change nickname\r\n");
				strcat(buff_out, "\\PRIVATE  <reference> <message> Send private message\r\n");
				strcat(buff_out, "\\ACTIVE   Show active clients\r\n");
				strcat(buff_out, "\\HELP     Show help\r\n");
				send_message_self(buff_out, client->connfd);
			}else{
				send_message_self("<<UNKOWN COMMAND\r\n", client->connfd);
			}
		}else{
			/* Send message */
			snprintf(buff_out, sizeof(buff_out), "[%s] %s\r\n", client->name, buff_in);
			send_message(buff_out, client->id);
		}
	}

	/* Close connection */
	sprintf(buff_out, "<<LEAVE, BYE %s\r\n", client->name);
	send_message_all(buff_out);
	close(client->connfd);

	/* Delete client from queue and yield thread */
	delete_cli_from_queue(client->id);
	printf("<<LEAVE ");
	print_client_addr(client->addr);
	printf(" REFERENCED BY %d\n", client->id);
	free(client);
	cli_count--;
	pthread_detach(pthread_self());

	return NULL;
}

int main(int argc, char *argv[]){
	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr;
	struct sockaddr_in cli_addr;
	pthread_t tid;

	/* Socket settings */
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(5000);

	/* Ignore pipe signals */
	signal(SIGPIPE, SIG_IGN);

	/* Bind */
	if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
		perror("Socket binding failed");
		return 1;
	}

	/* Listen */
	if(listen(listenfd, 10) < 0){
		perror("Socket listening failed");
		return 1;
	}

	printf("<[SERVER STARTED]>\n");

	/* Accept clients */
	while(1){
		socklen_t clilen = sizeof(cli_addr);
		connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

		/* Check if max clients is reached */
		if((cli_count+1) == MAX_CLIENTS){
			printf("<<MAX CLIENTS REACHED\n");
			printf("<<REJECT ");
			print_client_addr(cli_addr);
			printf("\n");
			close(connfd);
			continue;
		}

		/* Client settings */
		client_struct *client= (client_struct *)malloc(sizeof(client_struct));
		client->addr = cli_addr;
		client->connfd = connfd;
		client->id = id++;
		sprintf(client->name, "%d", client->id);

		/* Add client to the queue and fork thread */
		add_queue(client);
		pthread_create(&tid, NULL, &handle_client, (void*)client);

		/* Reduce CPU usage */
		sleep(1);
	}
}

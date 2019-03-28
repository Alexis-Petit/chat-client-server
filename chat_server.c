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
	struct sockaddr_in addr;	/* adresse Client à distance */
	int connfd;					/* Connection file descriptor */
	int id;						/* id du Client unique */
	char name[32];				/* nom du Client */
} client_struct;

client_struct *clients[MAX_CLIENTS];

/* Ajoute le client a la file */
void add_queue(client_struct *cl){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(!clients[i]){
			clients[i] = cl;
			return;
		}
	}
}

/* Supprime le client de la file */
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

/* Envois un message à tous les clients sauf l'emetteur */
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

/* Envois un message à tous les clients */
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

/* Envois un message à l'emetteur */
void send_message_self(const char *s, int connfd){
	if(write(connfd, s, strlen(s))<0){
		perror("write");
		exit(-1);
	}
}

/* Envois un message au client */
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

/* Envois un message a la liste de clients actifs */
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

/* Enleve les retour chariot */
void strip_newline(char *s){
	while(*s != '\0'){
		if(*s == '\r' || *s == '\n'){
			*s = '\0';
		}
		s++;
	}
}

/* Affiche l'adresse ip */
void print_client_addr(struct sockaddr_in addr){
	printf("%d.%d.%d.%d\n",
		addr.sin_addr.s_addr & 0xFF,
		(addr.sin_addr.s_addr & 0xFF00)>>8,
		(addr.sin_addr.s_addr & 0xFF0000)>>16,
		(addr.sin_addr.s_addr & 0xFF000000)>>24);
}

/* Gere la communication avec tous les clients */
void *handle_client(void *arg){
	char buff_out[2048];
	char buff_in[1024];
	int rlen;

	cli_count++;
	client_struct *client= (client_struct *)arg;

	printf("<<Nouveau client sur ");
	print_client_addr(client->addr);
	printf("id client %d\n", client->id);

	sprintf(buff_out, "<<Client : %s connectee. \n\r", client->name);
	send_message_all(buff_out);

	/* Recois les donnees du client */
	while((rlen = read(client->connfd, buff_in, sizeof(buff_in)-1)) > 0){
	        buff_in[rlen] = '\0';
	        buff_out[0] = '\0';
		strip_newline(buff_in);

		/* Ignore les buffer vide */
		if(!strlen(buff_in)){
			continue;
		}

		/* Options specials */
		if(buff_in[0] == '\\'){
			char *command, *param;
			command = strtok(buff_in," ");
			if(!strcmp(command, "\\QUIT")){
				break;
			}else if(!strcmp(command, "\\PING")){
				send_message_self("<<PONG\r\n", client->connfd);
			}else if(!strcmp(command, "\\RENAME")){
				param = strtok(NULL, " ");
				if(param){
					char *old_name = strdup(client->name);
					strcpy(client->name, param);
					sprintf(buff_out, "<<RENAME, %s EN %s\r\n", old_name, client->name);
					free(old_name);
					send_message_all(buff_out);
				}else{
					send_message_self("<<Le nom ne peut etre null\r\n", client->connfd);
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
						send_message_self("<<Le message ne peut etre null\r\n", client->connfd);
					}
				}else{
					send_message_self("<<La reference ne peut etre null\r\n", client->connfd);
				}
			}else if(!strcmp(command, "\\LIST")){
				int i;
				for( i = 0; i < MAX_CLIENTS; i++)
				{
					if(clients[i]){
						sprintf(buff_out, "<<Id : %d Nom : %s\r\n", clients[i]->id, clients[i]->name);
						send_message_self(buff_out, client->connfd);
					}
				}
				strcat(buff_out, "\r\n");
			}else if(!strcmp(command, "\\HELP")){
				strcat(buff_out, "\\HELP     Liste commandes\r\n");
				strcat(buff_out, "\\QUIT     Quitte le chatroom\r\n");
				strcat(buff_out, "\\PING     Interroge le serveur\r\n");
				strcat(buff_out, "\\RENAME   <name> Change le surnom\r\n");
				strcat(buff_out, "\\PRIVATE  <id dest> <msg> message prive\r\n");
				strcat(buff_out, "\\LIST     liste clients actifs\r\n");
				send_message_self(buff_out, client->connfd);
			}else{
				send_message_self("<<commande inconnu\r\n", client->connfd);
			}
		}else{
			/* Envois un message */
			snprintf(buff_out, sizeof(buff_out), "[%s] %s\r\n", client->name, buff_in);
			send_message(buff_out, client->id);
		}
	}

	/* Ferme les connections */
	sprintf(buff_out, "<<A plus ! %s\r\n", client->name);
	send_message_all(buff_out);
	close(client->connfd);

	/* Supprime un client de la file et supprime le thread */
	delete_cli_from_queue(client->id);
	printf("<<Aurevoir ");
	print_client_addr(client->addr);
	printf(" id client %d\n", client->id);
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

	/* Paramètre des sockets */
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(5000);

	/* Ignore les pipe signals */
	signal(SIGPIPE, SIG_IGN);

	if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
		perror("Echec de l'association de socket");
		return 1;
	}

	/* Ecoute */
	if(listen(listenfd, 10) < 0){
		perror("Echec de l'ecoute de socket");
		return 1;
	}

	printf("<[SERVEUR DEMARRE]>\n");

	/* Accepter les clients */
	while(1){
		socklen_t clilen = sizeof(cli_addr);
		connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

		/* Regarde si on peut encore accepter des clients */
		if((cli_count+1) == MAX_CLIENTS){
			printf("<<CLIENTS MAX ATTEINT\n");
			printf("<<REJETE ");
			print_client_addr(cli_addr);
			printf("\n");
			close(connfd);
			continue;
		}

		/* Parametre des clients */
		client_struct *client= (client_struct *)malloc(sizeof(client_struct));
		client->addr = cli_addr;
		client->connfd = connfd;
		client->id = id++;
		sprintf(client->name, "%d", client->id);

		/* ajoute le client à la file et fork un thread */
		add_queue(client);
		pthread_create(&tid, NULL, &handle_client, (void*)client);

		/* Reduit l'usage du CPU */
		sleep(1);
	}
}

all:
	$(CC) -Wall chat_server.c -O2 -lpthread -o chat_server
	$(CC) -Wall chat_client.c -O2 -lpthread -o chat_client

clean:
	$(RM) -rf chat_client 
	$(RM) -rf chat_client

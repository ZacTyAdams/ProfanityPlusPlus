#include <stdlib.h>
#include <sys/types.h>	/* system type defintions */
#include <sys/socket.h>	/* network system functions */
#include <netinet/in.h>	/* protocol & struct definitions */
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <sqlite3.h>
#include <regex.h>
#include <unistd.h>
extern void *client_handler(void *);

static const int NUM_THREADS = 1;
static int listener;

static void SIGINT_handler(int signum) {
    puts("\b\bServer shutting down.");
    close(listener);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 2 || atoi(argv[1]) < 1024) {
        puts("Must indicate listening port");
        return 0;
    }

    int	*handler, client_addrlen;
    struct sockaddr_in my_addr, client_addr;
    pthread_t thread;

    signal(SIGINT, SIGINT_handler);
    //signal(SIGSEGV, SIGINT_handler);

    //create socket for listening
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) {
        puts("socket() failed");
        exit(0);
    }

	//make local address structure
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    my_addr.sin_port = htons((unsigned short)atoi(argv[1]));
	
    //bind socket to the local address
    if (bind(listener, (struct sockaddr *) &my_addr, sizeof(my_addr)) < 0) {
        puts("bind() failed");
        close(listener);
        exit(0);
    }

	//listen
    if (listen(listener, NUM_THREADS) < 0) {
        puts("listen() failed");
        close(listener);
        exit(0);
    }
    
    while (1) {
        client_addrlen = sizeof(struct sockaddr_in);
        handler = (int *)malloc(sizeof(int));
        *handler = accept(listener, (struct sockaddr *) &client_addr, (socklen_t *)&client_addrlen);
        //client_handler((void *)handler);
        //Launch the new thread
    	pthread_create(&thread, NULL, client_handler, (void *)handler);
        pthread_detach(thread);
    }
}
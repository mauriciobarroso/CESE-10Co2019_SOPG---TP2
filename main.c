/********************************[ inclusions ]*************************************/

#include <stdio.h>
#include <stdlib.h>
#include "SerialManager.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>

/********************************[ macros ]*****************************************/

#define BUFFER_MAX_SIZE	128			// tamaño maximo del buffer
#define PORT_NUMBER		10000		// puerto de conexión del socket
#define NET_ADDR		"127.0.0.1"	// dirección IP del server
#define UART_NUM		1 			// número de identificador del puerto serie utilizado
#define UART_BAUDRATE	115200 		// baudrate del puerto serie
#define LISTE_BACKLOG	10			// backlog de listen

/********************************[ global data ]************************************/

char buffer[ BUFFER_MAX_SIZE ];	// buffer para guardar los mensajes de la UART y el socket
int sockfd;						// file descriptor del socket server para escuchar conexiones nuevas
int newSockfd; 					// file descriptor del socket server para leer y escribir el socket
pthread_t thread;				// array para los threads
pthread_t thread1;

/********************************[ functions declaration ]**************************/

/* función para bloquear signals */
void blockSig( void );

/* función para desbloquear signals */
void unblockSig( void );

/* handler para manejo de SIGINT y SIGTERM */
void sigHandler( int sig );

/* handler para el thread que recibe del socket y manda a la UART */
void * receiveFromSocketSendToUart( void * parameters );

/* handler para el thread que recibe de la UART y manda al socket */
void * receiveFromUartSendToSocket( void * parameters );

/********************************[ main function ]**********************************/

int main()
{
	/* se declaran las estructuras y variables para el socket */
	socklen_t addrLen;
	struct sockaddr_in clientAddr;
	struct sockaddr_in serverAddr;
	struct sigaction sa;

	/* se configura SIGINT y SIGTERM */
    sa.sa_handler = sigHandler;
    sa.sa_flags = 0;
    if( sigemptyset( &sa.sa_mask ) == -1)
    {
        perror( "sigemptyset" );
        exit( 1 );
    }
    if( sigaction( SIGINT, &sa, NULL ) == -1 )
    {
        perror( "sigaction" );
        exit( 1 );
    }

    if( sigemptyset( &sa.sa_mask ) == -1)
    {
        perror( "sigemptyset" );
        exit( 1 );
    }
    if( sigaction( SIGTERM, &sa, NULL ) == -1 )
    {
        perror( "sigaction" );
        exit( 1 );
    }

	/* se bloquean las señales */
	blockSig();

	/* se abre el puerto serial */
	if( serial_open( UART_NUM, UART_BAUDRATE ) == 1 )
		exit( 1 );

	/* se crear el socket para el server */
	sockfd = socket( PF_INET,SOCK_STREAM, 0 );

	/* se cargan los datos de IP:PORT del server */
    bzero( ( char * )&serverAddr, sizeof( serverAddr ) );
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons( PORT_NUMBER );
    serverAddr.sin_addr.s_addr = inet_addr( NET_ADDR );
    
	/* manejo de errores */
	if( serverAddr.sin_addr.s_addr == INADDR_NONE )
    {
        fprintf( stderr, "error invalid server IP\n" );
        exit( 1 );
    }

	/* se abre el puerto con bind */
	if ( bind( sockfd, ( struct sockaddr * )&serverAddr, sizeof( serverAddr ) ) == -1 ) 
	{
		close( sockfd );
		perror( "bind" );
		exit( 1 );
	}

	/* se setea el socket en listen */
	if ( listen( sockfd, LISTE_BACKLOG ) == -1 )
  	{
    	perror( "listen" );
    	exit( 1 );
  	}

	/* se crea un thread para recibir de la UART y mandar al socket */
	pthread_create ( &thread, NULL, receiveFromUartSendToSocket, NULL );

	/* se desbloquean las señales SIGINT y SIGTERM */
	unblockSig();

	/* infinite loop */ 
	for( ;; )
	{
		/* se ejecuta accept para recibir conexiones nuevas */
		printf( "esperando conexiones entrantes...\n" );
		addrLen = sizeof( struct sockaddr_in ); // se guarda en addrLen el tamaño de la estructura sockaddr_in
		newSockfd = accept( sockfd, ( struct sockaddr * )&clientAddr, &addrLen );
		if ( newSockfd == -1 )
		{
			perror( "accept" );
			exit(1);
		}

		printf  ( "nueva conexión desde %s\n", inet_ntoa( clientAddr.sin_addr ) ); // mensaje para informar de una nueva conexión

		/* se lanza la función que recibe del socket y envia a la UART */
		receiveFromSocketSendToUart( NULL );	
	}

	return 0;
}

/********************************[ functions definition ]***************************/

/* función para bloquear signals */
void blockSig( void )
{
	sigset_t set;
	int s;
	sigemptyset( &set );
	sigaddset( &set, SIGINT );
	sigaddset( &set, SIGTERM );
	pthread_sigmask( SIG_BLOCK, &set, NULL );
}

/* función para desbloquear signals */
void unblockSig( void )
{
	sigset_t set;
	int s;
	sigemptyset( &set );
	sigaddset( &set, SIGINT );
	sigaddset( &set, SIGTERM );
	pthread_sigmask( SIG_UNBLOCK, &set, NULL );
}

/* handler para manejo de SIGINT */
void sigHandler( int sig )
{
	pthread_cancel( thread );
	pthread_join( thread, NULL );
	close( newSockfd );
	close( sockfd );
	printf( "server terminado\n" );
	exit( 1 );
}

/* handler para el thread que recibe del socket y manda a la UART */
void * receiveFromSocketSendToUart( void * parameters )
{
	int n;

	for( ;; )
	{
		/* se leen los mensajes enviados por el socket cliente */
		n = read( newSockfd, buffer, BUFFER_MAX_SIZE );
		if( n == -1 )
		{
			perror( "read_socket" );
			return NULL;
		}

		else if ( n == 0 )
		{
			printf( "conexión cerrada\n" );
			close( newSockfd );
			return NULL;
		}

		buffer[ n ] = '\0';
		printf( "recibido por el socket %s\n", buffer ); // 

		/* se manda a la UART el mensaje */
		serial_send( buffer, n );
	}
	
	return NULL;
}

/* handler para el thread que recibe de la UART y manda al socket */
void * receiveFromUartSendToSocket( void * parameters )
{
	int n;

	for( ;; )
	{	
		/* se leen los mensajes enviados por la EDU-CIAA */
		n = serial_receive( buffer, BUFFER_MAX_SIZE );
		if( n > 0 )
		{
			buffer[ n - 2 ] = '\0'; // se restan 2 unidades para eliminar "\r\n"
			printf( "recibido por la uart %s\n", buffer );
			
			/* se envian los mensajes al socket */
			if( write( newSockfd, buffer, strlen( buffer ) ) == -1 )
			{
				perror( "socket_write" );
				return NULL;
			}
		}

		/* tiempo de refresco del polling */
		usleep( 50000 );
	}
	
	return NULL;
}
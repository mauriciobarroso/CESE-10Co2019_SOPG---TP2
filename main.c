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

#define BUFFER_MAX_SIZE		128			// tamaño maximo del buffer
#define PORT_NUMBER			10000		// puerto de conexión del socket
#define NET_ADDR			"127.0.0.1"	// dirección IP del server
#define UART_NUM			1 			// número de identificador del puerto serie utilizado
#define UART_BAUDRATE		115200 		// baudrate del puerto serie
#define LISTE_BACKLOG		1			// backlog de listen
#define ERROR_FUNCTION		0			// valor de retorno si existe un error
#define CLIENT_DISCONNECT	1			// valor de retorno si se desconecta el cliente
#define SERVER_CLOSE		2			// valor de retorno si se cierra el server

/********************************[ global data ]************************************/

char buffer[ BUFFER_MAX_SIZE ];							// buffer para guardar los mensajes de la UART y el socket
int sockfd;												// file descriptor del socket server para escuchar conexiones nuevas
int newSockfd = 0; 										// file descriptor del socket server para leer y escribir el socket
pthread_t thread;										// array para los threads
pthread_mutex_t mutexData = PTHREAD_MUTEX_INITIALIZER;	// mutex para la UART

/********************************[ functions declaration ]**************************/

/* función para bloquear signals */
void blockSig( void );

/* función para desbloquear signals */
void unblockSig( void );

/* handler para manejo de SIGINT y SIGTERM */
void sigHandler( int sig );

/* handler para el thread que recibe del socket y manda a la UART */
int receiveFromSocketSendToUart( void * parameters );

/* handler para el thread que recibe de la UART y manda al socket */
void * receiveFromUartSendToSocket( void * parameters );

/* función para terminar el server */
void closeServer( void );

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
	sockfd = socket( PF_INET, SOCK_STREAM, 0 );
	if( sockfd == -1 )
	{
		perror( "socket" );
        exit( 1 );
	}

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

	printf( "server iniciado\n" );

	/* se crea un thread para recibir de la UART y mandar al socket */
	if( pthread_create ( &thread, NULL, receiveFromUartSendToSocket, NULL ) == -1 )
	{
		perror( "pthread_create" );
		exit( 1 );
	}

	/* se desbloquean las señales SIGINT y SIGTERM */
	unblockSig();

	/* infinite loop */ 
	for( ;; )
	{
		/* se imprime mensaje de espera */
		printf( "esperando conexiones entrantes...\n" );

		/* se ejecuta accept para recibir conexiones nuevas */
		addrLen = sizeof( struct sockaddr_in ); // se guarda en addrLen el tamaño de la estructura sockaddr_in
		newSockfd = accept( sockfd, ( struct sockaddr * )&clientAddr, &addrLen );
		
		/* se cierra el server si se detecta error en connect */
		if ( newSockfd == -1 )
			closeServer();

		printf  ( "nueva conexión desde %s\n", inet_ntoa( clientAddr.sin_addr ) ); // mensaje para informar de una nueva conexión

		/* se lanza la función que recibe del socket y envia a la UART y verifica si en algún momento se cierra el server*/
		if( receiveFromSocketSendToUart( NULL ) == SERVER_CLOSE )	
			closeServer();
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
	/* se cierra el socket creado con connect */
	close( newSockfd );
}

/* handler para el thread que recibe del socket y manda a la UART */
int receiveFromSocketSendToUart( void * parameters )
{
	int n;

	for( ;; )
	{
		/* se leen los mensajes enviados por el socket cliente */
		n = read( newSockfd, buffer, BUFFER_MAX_SIZE );

		/* se retorna el valor SEVER_CLOSE si read da error */
		if( n == -1 )
		{
			perror( "read_socket" );
			return SERVER_CLOSE;
		}

		/* se retorna el valor CLIENT_DISCONNECT si read da 0 */
		else if ( n == 0 )
		{
			/* se bloquea el mutex */
			pthread_mutex_lock ( &mutexData );
			
			printf( "conexión cerrada\n" );
			close( newSockfd );
			newSockfd = 0;

			/* se desbloquea el mutex */
			pthread_mutex_unlock ( &mutexData );

			return CLIENT_DISCONNECT;
		}

		/* se imprimi un mensaje de recepción */
		buffer[ n ] = '\0';
		printf( "recibido por el socket %s\n", buffer ); // 

		/* se manda a la UART el mensaje */
		serial_send( buffer, n );
	}
	
	return ERROR_FUNCTION;
}

/* handler para el thread que recibe de la UART y manda al socket */
void * receiveFromUartSendToSocket( void * parameters )
{
	int n;

	for( ;; )
	{	
		/* se leen los mensajes enviados por la EDU-CIAA */
		n = serial_receive( buffer, BUFFER_MAX_SIZE );

		/* se bloquea el mutex */
		pthread_mutex_lock (&mutexData);

		if( n > 0 && newSockfd > 0 )
		{
			/* se imprimi un mensaje de recepción */
			buffer[ n - 2 ] = '\0'; // se restan 2 unidades para eliminar "\r\n"
			printf( "recibido por la uart %s\n", buffer );
			
			/* se envian los mensajes al socket */
			if( write( newSockfd, buffer, strlen( buffer ) ) == -1 )
			{
				perror( "socket_write" );
				return NULL;
			}
		}

		/* se desbloquea el mutex */
		pthread_mutex_unlock (&mutexData);

		/* tiempo de refresco del polling */
		usleep( 50000 );
	}
	
	return NULL;
}

/* función para terminar el server */
void closeServer( void )
{
	pthread_cancel( thread );
	pthread_join( thread, NULL );
	close( sockfd );
	printf( "server terminado\n" );
	exit( 1 );
}
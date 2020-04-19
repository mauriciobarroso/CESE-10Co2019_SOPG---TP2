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

#define BUFFER_MAX_SIZE	128
#define SOCKETS_MAX_NUM	2
#define PORT_NUMBER		10000
#define NET_ADDR		"127.0.0.1"
#define UART_NUM		2
#define UART_BAUDRATE	115200

/********************************[ global data ]************************************/

char buffer[ BUFFER_MAX_SIZE ];
int s;
int newfd;

pthread_t thread[ SOCKETS_MAX_NUM ];

/********************************[ functions declaration ]**************************/

/* función para bloquear signals */
void blockSign( void )
{
	sigset_t set;
	int s;
	sigemptyset( &set );
	sigaddset( &set, SIGINT );
	sigaddset( &set, SIGTERM );
	pthread_sigmask( SIG_BLOCK, &set, NULL );
}

/* función para desbloquear signals */
void unblockSign( void )
{
	sigset_t set;
	int s;
	sigemptyset( &set );
	sigaddset( &set, SIGINT );
	sigaddset( &set, SIGTERM );
	pthread_sigmask( SIG_UNBLOCK, &set, NULL );
}

/* handler para manejo de SIGINT */
void sigintHandler( int sig )
{
	write(0, "Ahhh! SIGINT!\n", 14);
	serial_close();
	close( newfd );
	close( s );
	//exit( 1 );
}

/* handler para el thread que recibe del socket y manda a la UART */
void * receiveFromSocketSendToUart( void * parameters );

/* handler para el thread que recibe de la UART y manda al socket */
void * receiveFromUartSendToSocket( void * parameters );


/********************************[ main function ]**********************************/

int main()
{
	socklen_t addrLen;
	struct sockaddr_in clientAddr;
	struct sockaddr_in serverAddr;
	struct sigaction sa;
	
	/* se abre el puerto serial */
	if( serial_open( UART_NUM, UART_BAUDRATE ) == 1 )
		exit( 1 );

	/* se crear el socket para el server */
	s = socket( PF_INET,SOCK_STREAM, 0 );

	/* se cargan los datos de IP:PORT del server */
    bzero( ( char * )&serverAddr, sizeof( serverAddr ) );
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons( PORT_NUMBER );
    serverAddr.sin_addr.s_addr = inet_addr( NET_ADDR );
    
	/* manejo de errores */
	if( serverAddr.sin_addr.s_addr == INADDR_NONE )
    {
        fprintf( stderr,"ERROR invalid server IP\r\n" );
        return 1;
    }

	/* se abre el puerto con bind */
	if ( bind(s, ( struct sockaddr * )&serverAddr, sizeof( serverAddr ) ) == -1 ) 
	{
		close( s );
		perror( "bind" );
		return 1;
	}

	/* se pone el socket en listen */
	if ( listen( s, 10 ) == -1 ) // backlog=10
  	{
    	perror( "listen" );
    	exit( 1 );
  	}

    /* signal action: SIGINT */
    sa.sa_handler = sigintHandler;
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

	/* signal action: SIGTERM */
    sa.sa_handler = sigintHandler;
    sa.sa_flags = 0;
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
	blockSign();

	/* infinite loop */ 
	for( ;; )
	{
		/* se ejecuta accept para recibir conexiones nuevas */
		addrLen = sizeof( struct sockaddr_in );
		newfd = accept( s, ( struct sockaddr * )&clientAddr, &addrLen );
		if ( newfd == -1 )
		{
			perror( "accept" );
			exit(1);
		}

		printf  ( "server: new connection from %s\n", inet_ntoa( clientAddr.sin_addr ) );

		/* se crea un nuevo thread para recibir del socket */
		pthread_create ( &thread[ 0 ], NULL, receiveFromSocketSendToUart, NULL );
		pthread_create ( &thread[ 1 ], NULL, receiveFromUartSendToSocket, NULL );

		/* se desbloquean los señales */
		unblockSign();

		/* espera a que el thread[ 0 ] termine y cancela el thread[ 1 ] */
		pthread_join( thread[ 0 ], NULL );
		printf( "server: closed connection from %s\n", inet_ntoa( clientAddr.sin_addr ) );
		pthread_cancel( thread[ 1 ] );
	}

	return 0;
}

/********************************[ functions definition ]***************************/

void * receiveFromSocketSendToUart( void * parameters )
{
	int n;

	for( ;; )
	{
		/* se leen los mensajes enviados por el socket cliente */
		n = read( newfd, buffer, BUFFER_MAX_SIZE );
		if( n == -1 )
		{
			perror( "read_socket" );
			return NULL;
		}

		else if ( n == 0 )
		{
			close( newfd );
			return NULL;
		}

		buffer[ n ] = '\0';
		printf( "recibido por el socket %s\n", buffer );

		/* se manda a la UART el mensaje */
		serial_send( buffer, 8 );
	}
	
	return NULL;
}

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
			if( write( newfd, buffer, strlen( buffer ) ) == -1 )
			{
				perror( "socket_write" );
				close( newfd );
				return NULL;
			}
		}
		/* tiempo de refresco del polling */
		usleep( 5000);
	}
	
	return NULL;
}
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/**
 * @brief il seguente file contiene l'implementazione delle interfacce fornite
 * 		e l'aggiunta di altre funzioni necessarie alla comunicazione 
 * 		server -> client
 * @warning server e client utilizzano le stesse interfacce per lettura/scrittura
 * 
 * @file connections.c
 * @author Marco Costa - 545144
 * @date 2018-08-28
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include "utils.h"
#include "connections.h"
#include "message.h"

#define ERROR_CONNECTION -1
#define CLOSED_CONNECTION 0

static unsigned int max_allocable_buffer = 13107200; /* 100 mb in bytes */

/**
 * @function openConnection
 * @brief Apre una connessione AF_UNIX verso il server 
 *
 * @param path Path del socket AF_UNIX 
 * @param ntimes numero massimo di tentativi di retry
 * @param secs tempo di attesa tra due retry consecutive
 *
 * @return il descrittore associato alla connessione in caso di successo
 *         -1 in caso di errore
 */
int openConnection(char *path, unsigned int ntimes, unsigned int secs)
{
	if (strlen(path) > UNIX_PATH_MAX)
	{
		fprintf(stderr, "Unix_path_max exceeded\n");
		return -1;
	}

	/* se i valori sono maggiori imposto quelli di default */
	if (ntimes > MAX_RETRIES)
		ntimes = MAX_RETRIES;
	if (secs > MAX_SLEEPING)
		secs = MAX_SLEEPING;

	int fd_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd_sock == -1)
		return -1;

	struct sockaddr_un sa;
	init_sockaddr(&sa, path);

	for (int i = 0; i < ntimes; i++)
	{
		int ret_value;

		ret_value = connect(fd_sock, (struct sockaddr *)&sa, sizeof(sa));
		if (ret_value != -1)
			return fd_sock;

		if (errno == ENOENT)
			sleep(secs);
		else
		{
			close(fd_sock);
			return -1;
		}
	}

	/* connessione non stabilita */
	close(fd_sock);
	return -1;
}

/**
 * @brief verifica il valore di ritorno di read e write e lo confronta
 * 			con il valore atteso. 
 * 		In caso di errore restituisce l'errore opportuno e setta errno
 * 
 */
#define check_read_write(ret, size)                       \
	if ((ret) == -1)                                       \
		return ERROR_CONNECTION;  /* errno is set by foo */ \
	else if ((ret) == 0)			  /* closed conn */         \
		return CLOSED_CONNECTION; /* errno set by foo */    \
	else if ((ret) != (size))                              \
	{                                                      \
		errno = EIO; /* i/o error */                        \
		return ERROR_CONNECTION;                            \
	}

/**
 * @brief invia l'header del messaggio 
 * 
 * @param fd descrittore della connessione
 * @param hdr puntatore all'header del messaggio da ricevere
 * @return int #byte inviati se operazione a buon fine
 * 			<=0 se c'e' stato un errore 
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa)
 */
int sendHeader(int fd, message_hdr_t *hdr)
{

	message_hdr_t *temp = hdr;
	ssize_t remaining = sizeof(message_hdr_t);

	/* utilizziamo sempre un ciclo for per l'invio e la ricezione per 
		essere sicuri che la write/read non inviino/leggano meno byte
		del previsto */
	for (;;)
	{
		if (remaining <= 0)
			break;
		ssize_t write_bytes = write(fd, temp, remaining);
		if ((write_bytes > 0) && (write_bytes < remaining))
		{
			remaining -= write_bytes;
			temp += write_bytes;
			continue;
		}

		check_read_write(write_bytes, remaining);
		break;
	}

	return sizeof(message_hdr_t);
}
/**
 * @function readHeader
 * @brief Legge l'header del messaggio
 *
 * @param fd     descrittore della connessione
 * @param hdr    puntatore all'header del messaggio da ricevere
 *
 * @return #byte inviati se operazione a buon fine
 * 			<=0 se c'e' stato un errore 
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa) 
 */
int readHeader(long connfd, message_hdr_t *hdr)
{
	ssize_t hdr_size = sizeof(message_hdr_t);
	message_hdr_t *remaining = hdr;

	for (;;)
	{
		if (hdr_size <= 0)
			break;
		ssize_t read_bytes = read(connfd, remaining, hdr_size);
		if ((read_bytes > 0) && (read_bytes < hdr_size))
		{
			hdr_size -= read_bytes;
			remaining += read_bytes;
			continue;
		}
		check_read_write(read_bytes, hdr_size);
		break;
	}

	return sizeof(message_hdr_t);
}

/**
 * @function sendData
 * @brief Invia il body del messaggio al server
 *
 * @param fd     descrittore della connessione
 * @param msg    puntatore al messaggio da inviare
 *
 * @return #byte inviati se operazione a buon fine
 * 			<=0 se c'e' stato un errore 
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa)
 */
int sendData(long fd, message_data_t *msg)
{
	message_data_hdr_t *temp = &(msg->hdr);
	ssize_t remaining = sizeof(message_data_hdr_t);

	for (;;)
	{
		if (remaining <= 0)
			break;
		ssize_t write_bytes = write(fd, temp, remaining);
		if ((write_bytes > 0) && (write_bytes < remaining))
		{
			remaining -= write_bytes;
			temp += write_bytes;
			continue;
		}

		check_read_write(write_bytes, remaining);
		break;
	}

	/* else */
	ssize_t buf_size = msg->hdr.len * sizeof(char);

	if (buf_size < 0)
		return ERROR_CONNECTION;
	if (buf_size == 0) /* messaggio senza buffer */
		return sizeof(message_data_hdr_t);
	if (buf_size > max_allocable_buffer)
	{
		fprintf(stderr, "[!!] massima dimensione buffer inviabile: %d\n", max_allocable_buffer);
		return ERROR_CONNECTION;
	}

	char *curr_pos = msg->buf;
	ssize_t curr_buff_size = buf_size;
	for (;;)
	{
		if (curr_buff_size <= 0)
			break;
		ssize_t new_write_bytes = write(fd, curr_pos, curr_buff_size);
		if ((new_write_bytes > 0) && (new_write_bytes < curr_buff_size))
		{
			curr_pos += new_write_bytes;
			curr_buff_size -= new_write_bytes;
			continue;
		}
		check_read_write(new_write_bytes, curr_buff_size);
		break;
	}

	return sizeof(message_data_hdr_t) + buf_size;
}

/**
 * @function readData
 * @brief Legge il body del messaggio
 *
 * @param fd     descrittore della connessione
 * @param data   puntatore al body del messaggio
 *
 * @return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa) 
 */
int readData(long fd, message_data_t *data)
{
	data->buf = NULL;

	message_data_hdr_t *temp = &(data->hdr);
	ssize_t remaining = sizeof(message_data_hdr_t);

	for (;;)
	{
		if (remaining <= 0)
			break;
		ssize_t read_bytes = read(fd, temp, remaining);
		if ((read_bytes > 0) && (read_bytes < remaining))
		{
			remaining -= read_bytes;
			temp += read_bytes;
			continue;
		}

		check_read_write(read_bytes, remaining);
		break;
	}

	ssize_t buf_dim = data->hdr.len * sizeof(char);

	if (buf_dim < 0)
		return ERROR_CONNECTION;
	if (buf_dim == 0) /* messaggio senza buffer */
		return sizeof(message_hdr_t);
	if (buf_dim > max_allocable_buffer)
		return ERROR_CONNECTION;

	/* else */
	data->buf = malloc(buf_dim);
	if (!(data->buf))
	{
		fprintf(stderr, STRING_BAD_MALLOC);
		return ERROR_CONNECTION;
	}

	char *curr_pos = data->buf;
	ssize_t remaining_buff_size = buf_dim;

	for (;;)
	{
		if (remaining_buff_size <= 0)
			break;
		ssize_t new_read_bytes = read(fd, curr_pos, remaining_buff_size);
		if ((new_read_bytes > 0) && (new_read_bytes < remaining_buff_size))
		{
			curr_pos += new_read_bytes;
			remaining_buff_size -= new_read_bytes;
			continue;
		}
		check_read_write(new_read_bytes, remaining_buff_size);
		break;
	}

	return sizeof(message_hdr_t) + buf_dim;
}

/**
 * @brief interfaccia per la lettura di un intero messaggio header + data
 * 
 * @param fd descrittore della connessione
 * @param msg puntatore al messaggio da inviare
 * @return int #byte inviati se operazione a buon fine
 * 			<=0 se c'e' stato un errore 
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa)
 */
int readMsg(long fd, message_t *msg)
{
	memset(msg, 0, sizeof(message_t));

	int first_read = readHeader(fd, &(msg->hdr));

	if (first_read <= 0)
		return first_read;

	int second_read = readData(fd, &(msg->data));
	if (second_read <= 0)
		return second_read;

	/* DEBUG */
	return first_read + second_read;
}

/**
 * @function sendRequest
 * @brief Invia un messaggio di richiesta al server 
 *
 * @param fd     descrittore della connessione
 * @param msg    puntatore al messaggio da inviare
 *
 * @return #byte inviati se operazione a buon fine
 * 			<=0 se c'e' stato un errore 
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa)
 */
int sendRequest(long fd, message_t *msg)
{
	int first_write = sendHeader(fd, &(msg->hdr));
	if (first_write <= 0)
		return first_write;

	int second_write = sendData(fd, &(msg->data));
	if (second_write <= 0)
		return second_write;

	/* else */
	return first_write + second_write;
}

inline void init_sockaddr(struct sockaddr_un *sa, char *sockname)
{
	sa->sun_family = AF_UNIX;
	strncpy(sa->sun_path, sockname, strlen(sockname) + 1);
}

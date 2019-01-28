#define _GNU_SOURCE

/**
 * @brief contiene:
 * 		- la routine principale di ricezione messaggio del server
 * 		- inizializzazione impostazioni del database (crezione schema, 
 * 						verifica accesso, ecc.)
 * 		- l'inizializzazione delle impostazioni del server (creazione socket, ecc.)
 * 		- routine di chiusura e pulizia del server e del database
 * @file core.c
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-08-27
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <assert.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "core.h"
#include "sqlite3.h"
#include "queries.h"
#include "utils.h"
#include "queues.h"
#include "slaves.h"
#include "connections.h"
#include "ops.h"
#include "config.h"

#define INACTIVE_THREAD 0

/* handler del database del core */
static sqlite3 *db = NULL;
/* garantisce l'apertura esclusiva di una connessione col database */
static pthread_mutex_t access_open_db = PTHREAD_MUTEX_INITIALIZER;

/* thread slaves */
static pthread_t **slaves = NULL;
static unsigned int tot_slaves;

/* necessario volatile sig_atomic_t in quanto vi si accede anche dal 
	signal handler */
static volatile sig_atomic_t server_running = 1;

/* socket del server */
static int server_sock;
static char *sock_name;

//-------------------------------------------------------------------------//

/**
 * @brief restituisce alcuni valori della configurazione
 */

static char *filepath;
static char *filestats;
static int max_msgs;

int get_maxmsgs()
{
	return max_msgs;
}
char *get_filepath()
{
	return filepath;
}

char *get_statsfile()
{
	return filestats;
}

//-------------------------------------------------------------------------//

int open_db(sqlite3 **db)
{
	pthread_mutex_lock(&access_open_db);
	int ret_value =
		 sqlite3_open_v2(DB_NAME, db,
							  SQLITE_OPEN_FULLMUTEX |
									SQLITE_OPEN_READWRITE |
									SQLITE_OPEN_CREATE,
							  NULL);
	pthread_mutex_unlock(&access_open_db);

	ret_value += sqlite3_exec(*db, "PRAGMA journal_mode = " JOURNAL_MODE ";", NULL, NULL, NULL);
	if (ret_value != SQLITE_OK)
	{
		fprintf(stderr, STRING_BAD_DB_OPEN, sqlite3_errmsg(*db));
		sqlite3_close(*db);
	}

	assert(sqlite3_threadsafe());
	return ret_value;
}

/**
 * @brief esegue l'inizializzazione del database (creazione schema, 
 * 			verifica presenza su disco, ripristino configurazione, ecc.)
 * 
 * @return int (SQLITE_OK) | (SQLITE_FAIL)
 */
static int init_core_db()
{
	int ret_value;
	char *err_msg = 0;

	/**
	 * @brief controllo accesso al file database, casi:
	 * 
	 * 1. se vi è un errore nel controllo del file restituisce errore 
	 * 2. se il file esiste si apre un handler del database
	 * 3. se il file non esiste si apre un handler e si effettua la query
	 * 	di creazione del database
	 * 
	 */

	ret_value = access(DB_NAME, R_OK | W_OK);
	if (ret_value == -1 && errno != ENOENT)
	{
		perror(STRING_BAD_DB_ACCESS);
		return ret_value;
	}

	if (open_db(&db) != SQLITE_OK)
		return EXIT_FAILURE;

	system("chmod 777 " DB_NAME "*");
	/* database già presente, devo resettare il valore dei file descriptor 
		associati agli utenti che potrebbero essere rimasti aperti in caso
		di terminazione precedente del server tramite segnale */
	if (ret_value == 0)
	{
		ret_value = sqlite3_exec(db, cleardb(), NULL, NULL, &err_msg);
		if (ret_value != SQLITE_OK)
		{
			BAD_QUERY(err_msg);
			sqlite3_close(db);
			return ret_value;
		}

		fprintf(stdout, STRING_LOG_DBOPENED);
		return EXIT_SUCCESS;
	}
	/* esecuzione query creazione database */
	ret_value = sqlite3_exec(db, createdb(), NULL, NULL, &err_msg);
	if (ret_value != SQLITE_OK)
	{
		BAD_QUERY(err_msg);
		sqlite3_close(db);
		return ret_value;
	}

	sqlite3_free(err_msg);

#ifdef LOG_MSG
	fprintf(stdout, STRING_LOG_DBCREATED);
#endif

	return EXIT_SUCCESS;
}

/**
 * @brief inizializza i thread, la coda e la socket
 * 
 * REQUIRES: slaves == NULL
 * @param conf 
 * @return int (EXIT_SUCCESS se l'inizializzazione è andata a buon fine, 
 * 					EXIT_FAILURE altrimenti)
 */
static int init_core_server(conf_param *conf)
{
	int i, ret_value;
	int slave_id_dim = strlen(SLAVE_NAME) + get_digits_number(conf->threads_in_pool) + 1;

	/**-----------------------------------------------------------------
	 * @brief inizializzazione della coda
	 ------------------------------------------------------------------*/

	queue_init();

	/**-----------------------------------------------------------------
	 * @brief inizializzazione dei thread
	 ------------------------------------------------------------------*/

	/* alloco la memoria necessaria in base al numero di thread specificati nel
	   file di configurazione */
	tot_slaves = conf->threads_in_pool;
	slaves = calloc(tot_slaves, tot_slaves * sizeof(pthread_t *));
	if (!slaves)
	{
		fprintf(stderr, STRING_BAD_MALLOC);
		return EXIT_FAILURE;
	}

	/* inizializzo la struttura dati condivisa */
	if (init_slaves(tot_slaves) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	for (i = 0; i < tot_slaves; i++)
	{
		char slave_id[slave_id_dim];
		unsigned int *slave_no;

		slaves[i] = safe_malloc(sizeof(pthread_t));
		slave_no = safe_malloc(sizeof(unsigned int));
		*slave_no = i;
		ret_value = pthread_create(slaves[i], NULL, &slave_routine, (void *)slave_no);
		if (ret_value != 0)
		{
			slaves[i] = INACTIVE_THREAD;
			handle_error(STRING_HANDLE_BAD_THREAD_CREATION);
		}
		snprintf(slave_id, sizeof(slave_id), SLAVE_NAME "%d", i);
		pthread_setname_np(**(slaves + i), slave_id); /* utile per il debug */
	}

	/**-----------------------------------------------------------------
	 * @brief inizializzazione della socket
	 ------------------------------------------------------------------*/

	sock_name = conf->unix_path;
	/* se è rimasto il file socket effettuo la rimozione */
	ret_value = access(sock_name, F_OK);
	/* errore: non ho accesso alla directory 
		o altro errore generico di permessi */
	if ((ret_value == -1) && (errno != ENOENT))
		handle_error(STRING_HANDLE_BAD_SOCKET_CREATION);
	if (ret_value == 0)
		unlink(sock_name);

	server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (server_sock == -1)
		handle_error(STRING_HANDLE_BAD_SOCKET_CREATION);

	struct sockaddr_un sa;
	init_sockaddr(&sa, (char *)sock_name);
	ret_value = bind(server_sock, (struct sockaddr *)&sa, sizeof(sa));
	if (ret_value != 0)
		handle_error(STRING_HANDLE_BAD_BIND);

	/**-------------------------------------------------------------------
	 * @brief inizializzazione filepath
	 --------------------------------------------------------------------*/
	filestats = conf->stat_filename;
	max_msgs = conf->max_hist_msg;
	filepath = conf->dir_name;
	ret_value = mkdir(conf->dir_name, S_IRWXU); /* rwx user */
	/* errore nella creazione directory */
	if (ret_value == -1 && errno != EEXIST)
		handle_error(STRING_HANDLE_BAD_FOLDER);

	return EXIT_SUCCESS;
}

/**
 * @brief routine del server sequenziale multi-client
 * 
 * @param conf struttura con i parametri di configurazione
 * @return int descrittore di comunicazione col signal handler
 */
int start_core(conf_param *conf, int efd)
{
	int ret_value;

#ifdef MAKE_VALGRIND_HAPPY
	system("rm " DB_NAME " > /dev/null");
#endif
/* è stata selezionate l'opzione per resettare il database ad ogni riavvio */
#ifdef RESET_DB
	if (access(DB_NAME, F_OK) == 0)
		system("rm " DB_NAME "*");
	if (access(conf->stat_filename, F_OK) == 0)
	{
		char *temp;
		asprintf(&temp, "rm %s", conf->stat_filename);
		system(temp);
		free(temp);
	}
	if (access(conf->dir_name, F_OK) == 0)
	{
		char *temp;
		asprintf(&temp, "rm -r %s", conf->dir_name);
		system(temp);
		free(temp);
	}
#endif

	/* inizializzazione configurazione database */
	ret_value = init_core_db(conf);
	if (ret_value != 0)
		return EXIT_FAILURE;

	/* inizializzazione configurazione server */
	ret_value = init_core_server(conf);
	if (ret_value != EXIT_SUCCESS)
	{
		stop_core();
		return EXIT_FAILURE;
	}

	/* inizio routine del server */
	fd_set active_set, read_set;
	int fd_num = -1;
	int new_client;

	ret_value = listen(server_sock, conf->max_connections + 2);
	if (ret_value != 0)
	{
		stop_core();
		handle_error(STRING_HANDLE_BAD_LISTEN);
	}

	FD_ZERO(&active_set);
	FD_SET(server_sock, &active_set);
	FD_SET(efd, &active_set); /* imposto il descrittore fasullo di terminazione */

	if (server_sock > efd)
		fd_num = server_sock;
	else
		fd_num = efd;

	/**
	 * @brief routine del server (select + accept)
	 * 
	 */
	while (server_running)
	{
		read_set = active_set;
		if (select(fd_num + 1, &read_set, NULL, NULL, NULL) < 0)
		{
			stop_core();
			handle_error(STRING_HANDLE_BAD_SELECT);
		}

		if (FD_ISSET(efd, &read_set))
		{
			uint64_t ret;
			if (read(efd, &ret, sizeof(uint64_t)) == sizeof(uint64_t))
			{
				printf("[!!] efd set\n");
				continue;
			}
		}

		for (int fd = 0; fd <= fd_num; fd++)
		{
			if (FD_ISSET(fd, &read_set))
			{
				/* nuovo client in ingresso */
				if (fd == server_sock)
				{
					new_client = accept(server_sock, NULL, NULL);
					/* non bloccante in caso di errore */
					if (new_client < 0)
						perror(STRING_PERROR(STRING_BAD_ACCEPT));
					else
					{
						FD_SET(new_client, &active_set);
						if (new_client > fd_num)
							fd_num = new_client;

#ifdef LOG_MSG
						fprintf(stdout, STRING_LOG_NEWCONN, new_client);
						fflush(stdout);
#endif
					}
				}
				else
				{
					message_t *new_message = safe_malloc(sizeof(message_t));

					ret_value = readMsg(fd, new_message);
					/* evitiamo di chiudere tutto il server per errori della read:
						se causa errori, lo trattiamo come una connessione chiusa */

					if (ret_value <= 0)
					{
						/* meglio pulire la memoria allocata */
						memset(new_message, 0, sizeof(message_t));

						/* imposto la disconnessione dell'utente in base al suo descrittore
							poiché non ne conosco il nome utente */
						new_message->hdr.op = DISCONNECT_OP;
						queue_push(new_message, fd);

						fprintf(stdout, "[++] connessione con fd %d chiusa\n", fd);
						FD_CLR(fd, &active_set);
					}
					/* in caso di file devo aspettare la seconda parte del messaggio */
					else if (new_message->hdr.op == POSTFILE_OP)
					{
						message_data_t *data = safe_malloc(sizeof(message_data_t));
						memset(data, 0, sizeof(message_data_t));
						ret_value = readData(fd, data);

						/* connessione chiusa */
						if (ret_value <= 0)
						{
							memset(data, 0, sizeof(message_data_t));

							new_message->hdr.op = DISCONNECT_OP;
							queue_push(new_message, fd);
							FD_CLR(fd, &active_set);

							if (data->buf)
								free(data->buf);
							free(data);
						}
						/* dimensione file permessa superata */
						else if (data->hdr.len > conf->max_file_size * 1024)
						{
							memset(new_message, 0, sizeof(message_t));
							new_message->hdr.op = OP_MSG_TOOLONG;
							queue_push(new_message, fd);

							free(data->buf);
							free(data);
						}
						/* inserisco tutto in un unico messaggio
							il buffer alla fine conterrà: "filename'\0'contenuto del file" */
						else
						{
							new_message->data.buf[new_message->data.hdr.len - 1] = '\0';
							char *temp = basename(new_message->data.buf);
							char *p = new_message->data.buf;
							new_message->data.hdr.len = strlen(temp) + data->hdr.len + 1;
							new_message->data.buf = safe_malloc(new_message->data.hdr.len * sizeof(char));
							strcpy(new_message->data.buf, temp);
							memcpy(&(new_message->data.buf[strlen(temp) + 1]), data->buf, data->hdr.len);

							free(p);
							free(data->buf);
							free(data);
							queue_push(new_message, fd);
						}
					}
					else if (new_message->data.hdr.len > conf->max_msg_size)
					{
						memset(new_message, 0, sizeof(message_t));
						/* liberiamo preventivamente la memoria allocata per il buffer */
						free(new_message->data.buf);
						new_message->data.buf = NULL;
						new_message->hdr.op = OP_MSG_TOOLONG;
						queue_push(new_message, fd);
					}
					/* mi è stato inviato un messaggio con una operazione
						riservata allo spazio applicativo */
					else if (new_message->hdr.op < 0)
					{
						memset(new_message, 0, sizeof(message_t));
						new_message->hdr.op = OP_FAIL;
						queue_push(new_message, fd);
					}
					/* tutto ok: passo il messaggio agli slaves */
					else
						queue_push(new_message, fd);
				}
			}
		}
	}

	/* chiudo i descrittori rimasti ancora aperti */
	for (int i = 0; i <= fd_num; i++)
		if (FD_ISSET(i, &active_set))
			close(i);

	return EXIT_SUCCESS;
}

/**
 * @brief indica la terminazione del server
 * @warning deve essere settato anche l'eventfd
 * 
 */
void stop_server()
{
	server_running = 0;
}

/**
 * @brief esegue la chiusura e pulizia di tutte le strutture allocate
 * 			ed esegue la join dei thread slaves
 * 
 */
void stop_core()
{
	int i;

	printf("[!!] stopping core \n");
	/* invio segnali di terminazione */
	queue_free();
	terminate_db();

	/* attendo la loro chiusura */
	for (i = 0; i < tot_slaves; i++)
	{
		if (slaves[i] != INACTIVE_THREAD)
		{
			pthread_join(**(slaves + i), NULL);
			free(slaves[i]);
			slaves[i] = INACTIVE_THREAD;
		}
	}

	/* pulisco il vettore di thread */
	if (slaves)
	{
		free(slaves);
		slaves = NULL;
	}

	/* pulisco le strutture dati condivise (coda ecc.) */
	destroy_slaves();
	destroy_queue_mutex();

	unlink(sock_name);

	if (db)
	{
		sqlite3_close(db);
		db = NULL;
	}

	pthread_mutex_destroy(&access_open_db);
}
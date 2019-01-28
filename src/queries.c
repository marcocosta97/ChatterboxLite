#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/**
 * @brief il seguente file contiene la query di creazione del database,
 * 		l'implementazione delle funzioni di callback per sqlite3  e
 * 		le funzioni per l'interfacciamento del server con il database
 * 		in base all'operazione richiesta
 * 
 * @file queries.c
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-08-27
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "sqlite3.h"
#include "message.h"
#include "utils.h"
#include "queries.h"
#include "core.h"
#include "stats.h"
#include "slaves.h"

//-------------------------------------------------------------------------//

/**-------------------------------------------------------------------------
 * @brief schema relazionale del database 
 * 		(si guardi la relazione per il diagramma)
 -------------------------------------------------------------------------*/
static const char query_createdb[] = QUOTE(
	 CREATE TABLE _User(
		  username varchar PRIMARY KEY,
		  curr_fd integer NOT NULL);
	 CREATE TABLE _Message(
		  message_id integer PRIMARY KEY AUTOINCREMENT,
		  message varchar,
		  filename varchar,
		  sent_by varchar NOT NULL,
		  chat_id integer NOT NULL,
		  sent_time datetime NOT NULL,
		  FOREIGN KEY(chat_id) REFERENCES _Chat(chat_id),
		  FOREIGN KEY(sent_by) REFERENCES _User(username));
	 CREATE TABLE _Chat(
		  chat_id integer PRIMARY KEY AUTOINCREMENT,
		  chat_name varchar UNIQUE,
		  creator varchar,
		  FOREIGN KEY(creator) REFERENCES _User(username));
	 CREATE TABLE _Chat_User(
		  chat_id integer NOT NULL,
		  username varchar NOT NULL,
		  PRIMARY KEY(chat_id, username),
		  FOREIGN KEY(chat_id) REFERENCES _Chat(chat_id),
		  FOREIGN KEY(username) REFERENCES _User(username));
	 CREATE TABLE _Stats(
		  not_delivered_txt integer NOT NULL,
		  not_delivered_file integer NOT NULL,
		  delivered_txt integer NOT NULL,
		  delivered_file integer NOT NULL,
		  error_numbers integer NOT NULL);
	 INSERT INTO _Stats VALUES('0', '0', '0', '0', '0'););

/**
 * @brief imposta tutti gli utenti come disconessi, da eseguire all'avvio
 * 		del server
 * 
 */
static const char query_cleardb[] = QUOTE(
	 UPDATE _User
		  SET curr_fd = -1;);

//-------------------------------------------------------------------------//

/**
 * @brief permette di controllare che il nome utente non inizi
 * 		con un carattere speciale (riservato per il database)
 * 
 */
#define is_validusername(string) \
	((string) && (!ispunct(*string)))

//-------------------------------------------------------------------------//

/**
 * @brief strutture di sincronizzazione necessarie all'esecuzione su
 * 
 */
static pthread_mutex_t access_db_op = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t db_busy = PTHREAD_COND_INITIALIZER;

/* non è necessario che sia volatile sig_atomic_t in quanto non ha 
	relazione con nessun signal handler */
static int terminate_queries = 0;

/**
 * @brief operazione corrente sul database
 * 
 */
enum _db_op
{
	reading,
	writing,
	noop /**< no operation */
};

typedef enum _db_op type_db_op;

static type_db_op db_op = noop;

/* numero di lettori contemporaneamente attivi */
static int no_reader = 0;

/**
 * @brief indica alle connessioni in coda nel database che è stata richiesta
 * 		la terminazione
 * 
 */
void terminate_db()
{
	terminate_queries = 1;
	pthread_cond_broadcast(&db_busy);
}

int exec_query(sqlite3 *db, char *query, int(callback)(void *, int, char **, char **), void *result)
{
	char *err = 0;
	int i, second_chance = 0;
	char query_op = *query;

#ifdef WAL_MODE
	/**
	 * @warning la modalità WAL permette la scrittura e lettura
	 * 		concorrente multipla sul database senza necessità di 
	 * 		sincronizzazione manuale
	 * 
	 * @warning NON TESTATA
	 */

	for (;;)
	{
		i = sqlite3_exec(db, query, callback, result, &err);

		if (second_chance)
			break;

		if ((i == SQLITE_BUSY) || (i == SQLITE_LOCKED))
		{
			second_chance = 1;
			continue;
		}

		break;
	}

#endif
#ifndef WAL_MODE
	type_db_op requested_op;

	/**
	 * @warning possiamo eseguire letture parallele del database 
	 * 			ma solo una scrittura per volta
	 */
	switch (query_op)
	{
	case 'S': /* select */
		requested_op = reading;
		break;
	case 'I': /* insert */
	case 'D': /* delete */
	case 'U': /* update */
	case 'C': /* create */
		requested_op = writing;
		break;
	default:
		fprintf(stderr, "[!!] Unknown query operation\n");
		exit(EXIT_FAILURE);
	}

	if (requested_op == reading)
	{
		pthread_mutex_lock(&access_db_op);
		while ((!terminate_queries) && (db_op == writing))
			pthread_cond_wait(&db_busy, &access_db_op);

		/* richiesta la terminazione */
		if (terminate_queries)
		{
			pthread_cond_broadcast(&db_busy);
			pthread_mutex_unlock(&access_db_op);
			sqlite3_free(err);
			return SQLITE_FAIL;
		}

		if (db_op == noop)
			db_op = reading;
		if (db_op == reading)
		{
			no_reader++; // incremento il numero di lettori
			pthread_mutex_unlock(&access_db_op);

			i = sqlite3_exec(db, query, callback, result, &err);

			pthread_mutex_lock(&access_db_op);
			no_reader--;
			/* se sono terminati i lettori imposto il db in no-operation */
			if (no_reader == 0)
			{
				db_op = noop;
				pthread_cond_signal(&db_busy); // sveglio un writer
			}
			pthread_mutex_unlock(&access_db_op);
		}
	}
	else // writing
	{
		for (;;)
		{
			pthread_mutex_lock(&access_db_op);
			while ((!terminate_queries) && (db_op != noop))
				pthread_cond_wait(&db_busy, &access_db_op);

			/* richiesta la terminazione */
			if (terminate_queries)
			{
				pthread_cond_broadcast(&db_busy);
				pthread_mutex_unlock(&access_db_op);
				sqlite3_free(err);
				return SQLITE_OK;
			}

			db_op = writing;
			pthread_mutex_unlock(&access_db_op);

			i = sqlite3_exec(db, query, callback, result, &err); // accesso esclusivo

			pthread_mutex_lock(&access_db_op);
			db_op = noop;
			pthread_cond_broadcast(&db_busy);
			pthread_mutex_unlock(&access_db_op);

			if (second_chance)
				break;

			if ((i == SQLITE_BUSY) || (i == SQLITE_LOCKED))
			{
				second_chance = 1;
				continue;
			}

			break; // query eseguita
		}
	}
#endif

	if ((i != SQLITE_OK) && (i != SQLITE_CONSTRAINT))
	{
		fprintf(stderr, "[!!] Errore nel database: %s -- %d\n", err, sqlite3_extended_errcode(db));
		sqlite3_free(err);
		exit(EXIT_FAILURE);
	}

	sqlite3_free(err);
	return i;
}

/**--------------------------------------------------------------------------
 * @brief 		interfacce per riempimento messaggi di risposta
 *--------------------------------------------------------------------------*/

void set_reply_message(message_t *ans, op_t OP, char *buffer, int buf_dim, char *sender, char *receiver)
{
	setData(&(ans->data), receiver, buffer, buf_dim);
	setHeader(&(ans->hdr), OP, sender);
}

void set_error_message(message_t *ans, op_t OP, char *buffer, int buf_dim, char *sender, char *receiver)
{
	char *ret = safe_malloc(buf_dim * sizeof(char));
	strncpy(ret, buffer, buf_dim * sizeof(char));

	set_reply_message(ans, OP, ret, buf_dim, sender, receiver);
}

/**--------------------------------------------------------------------------
 * @brief 						funzioni di callback
 *--------------------------------------------------------------------------*/

int getstringlist_callback(void *param, int argc, char **argv, char **col_name)
{
	struct callback_param_string *c = (struct callback_param_string *)param;

	/* per un qualche motivo il numero di utenti è maggiore della dimensione allocata */
	if (c->curr_pos >= c->size)
		return EXIT_SUCCESS;

	char *curr_pos = (c->result) + ((c->curr_pos * (MAX_NAME_LENGTH + 1)));
	if (argv[0])
		strcpy(curr_pos, argv[0]);
	c->curr_pos++;

	return EXIT_SUCCESS;
}

int getlong_callback(void *param, int argc, char **argv, char **col_name)
{
	long *par = (long *)param;
	if (argv[0])
		*par = strtol(argv[0], NULL, 10);
	else
		*par = GETLONG_ERROR;

	return EXIT_SUCCESS;
}

int getstats_callback(void *param, int argc, char **argv, char **col_name)
{
	unsigned long *vector = (unsigned long *)param;

	for (int i = 0; i < argc; i++)
		*(vector + i) = (argv[i]) ? strtol(argv[i], NULL, 10) : GETLONG_ERROR;

	return EXIT_SUCCESS;
}

int getlonglist_callback(void *param, int argc, char **argv, char **col_name)
{
	struct callback_param_long *c = (struct callback_param_long *)param;

	if (c->curr_pos >= c->size)
		return EXIT_SUCCESS;

	long *curr_pos = (c->result) + (c->curr_pos);
	*curr_pos = (argv[0]) ? strtol(argv[0], NULL, 10) : GETLONG_ERROR;

	(c->curr_pos)++;
	return EXIT_SUCCESS;
}

int getmessagelist_callback(void *param, int argc, char **argv, char **col_name)
{
	struct callback_param_message *m = (struct callback_param_message *)param;

	if (m->curr_pos >= m->size)
		return EXIT_SUCCESS;

	message_t *curr_pos = m->result + m->curr_pos;
	if (!curr_pos)
		return EXIT_FAILURE;
	curr_pos->data.buf = NULL;
	curr_pos->data.hdr.len = 0;
	/* message - filename - sent_by */
	if (argv[0]) /* message */
	{
		curr_pos->hdr.op = TXT_MESSAGE;
		curr_pos->data.hdr.len = strlen(argv[0]) + 1;
		curr_pos->data.buf = safe_malloc(curr_pos->data.hdr.len * sizeof(char));
		strcpy(curr_pos->data.buf, argv[0]);
	}
	else if (argv[1]) /* filename */
	{
		curr_pos->hdr.op = FILE_MESSAGE;
		curr_pos->data.hdr.len = strlen(argv[1]) + 1;
		curr_pos->data.buf = safe_malloc(curr_pos->data.hdr.len * sizeof(char));
		strcpy(curr_pos->data.buf, argv[1]);
	}
	else
		curr_pos->hdr.op = OP_FAIL;

	if (argv[2]) /* sent_by */
		strcpy(curr_pos->hdr.sender, argv[2]);

	(m->curr_pos)++;
	return EXIT_SUCCESS;
}
/*-------------------------------------------------------*/

/**
 * @brief - inserisce l'utente nel database e imposta come paramentro curr_fd l'attuale descrittore
 * 		 - chiama la funzione "exec_get_online_user" per mostrare la lista di utenti online
 * 
 * @param user utente da inserire nel database
 * @param *ans il messaggio di risposta (allocato dalla funzione)
 * @param db l'handler del database
 * @return 
 */
op_t manage_insertuser(char *user, int fd, message_t *ans, sqlite3 *db)
{
	int ret_value;
	long result;

	if (!is_validusername(user))
	{
		// non gestito dal client
		/* set_error_message(ans, OP_FAIL, STRING_CLIENT_USER_INVALIDCHAR,
								strlen(STRING_CLIENT_USER_INVALIDCHAR) + 1, "", ""); */
		return OP_FAIL;
	}

	/* dovrebbe essere garantito dal client, ma ricontrollare non fa male */
	if (strlen(user) > MAX_NAME_LENGTH)
	{
		/* set_error_message(ans, OP_FAIL, STRING_CLIENT_USER_NICK_TOOLONG,
								strlen(STRING_CLIENT_USER_NICK_TOOLONG) + 1, "", ""); */
		return OP_FAIL;
	}

	exec_checkexistname(db, user, &result);

	/* esiste già un utente o gruppo con quel nome */
	if (result != 0)
	{
		// set_error_message(ans, OP_NICK_ALREADY, STRING_CLIENT_USER_NICK_ALREADY, strlen(STRING_CLIENT_USER_NICK_ALREADY) + 1, "", "");
		return OP_NICK_ALREADY;
	}

	ret_value = exec_insertuser(db, user, fd);

	/* utente già presente */
	if (ret_value == SQLITE_CONSTRAINT)
		return OP_NICK_ALREADY;

	/* registrazione eseguita correttamente, richiedo lista utenti online */
	else
	{
		int buf_dim;
		char *s = exec_get_online_user(db, &buf_dim);
		set_reply_message(ans, OP_OK, s, buf_dim, "", "");
	}

	return OP_OK;
}

op_t manage_unregisteruser(char *user, sqlite3 *db)
{
	int ret_value;

	ret_value = exec_removeuser(db, user);
	if (ret_value != SQLITE_OK)
		return OP_FAIL;

	return OP_OK;
}

op_t manage_connectuser(char *user, int fd, message_t *ans, sqlite3 *db)
{
	int ret_value;
	long query_result;

	/* dovrebbe essere garantito dal client, ma ricontrollare non fa male */
	if (strlen(user) > MAX_NAME_LENGTH)
	{
		// non gestiti messaggi di errore dal client
		// set_error_message(ans, OP_FAIL, STRING_CLIENT_USER_NICK_TOOLONG, strlen(STRING_CLIENT_USER_NICK_TOOLONG) + 1, "", "");
		return OP_FAIL;
	}

	/**
	 * @brief controllo la presenza dell'utente nel database
	 * 
	 */
	exec_checkexistuser(db, user, &query_result);

	/* l'utente non è registrato */
	if (query_result != 1)
	{
		/* set_error_message(ans, OP_NICK_UNKNOWN, STRING_CLIENT_USER_NICK_UNKONWN,
								strlen(STRING_CLIENT_USER_NICK_UNKONWN) + 1, "", ""); */
		return OP_NICK_UNKNOWN;
	}

	/**
	 * @brief controlla che l'utente non sia già collegato
	 * 
	 */
	exec_getuserfd(db, user, &query_result);

	/* connessione successiva a registrazione, non devo fare nient'altro */
	if (query_result == fd)
	{
		int buf_dim;
		char *s = exec_get_online_user(db, &buf_dim);
		set_reply_message(ans, OP_OK, s, buf_dim, "", "");
#ifdef MAKE_TEST_HAPPY
		increase_sem_stats(sem_stats[1], -1);
#endif
		return OP_OK;
	}

	/* l'utente è già connesso */
	if (query_result != DISCONNECTED_FD)
	{
		/* set_error_message(ans, OP_FAIL, STRING_CLIENT_USER_BUSY,
								strlen(STRING_CLIENT_USER_BUSY) + 1, "", ""); */
		return OP_FAIL;
	}

	/* COND: l'utente non è connesso */

	/**
	 * @brief inserisce l'utente nel database come collegato
	 * 
	 */
	ret_value = exec_connectuser(db, user, fd);

	/* utente non presente */
	if (ret_value == SQLITE_CONSTRAINT)
	{
		/* set_error_message(ans, OP_NICK_UNKNOWN, STRING_CLIENT_USER_NICK_ALREADY,
								strlen(STRING_CLIENT_USER_NICK_ALREADY) + 1, "", ""); */
		return OP_NICK_UNKNOWN;
	}

	/* registrazione eseguita correttamente, richiedo lista utenti online */
	else
	{
		int buf_dim;
		char *s = exec_get_online_user(db, &buf_dim);
		set_reply_message(ans, OP_OK, s, buf_dim, "", "");
	}

	return OP_OK;
}

void manage_disconnectuser(int fd, sqlite3 *db)
{
	exec_disconnectuser(db, fd);
}

/**
 * @brief esegue l'inserimento di un messaggio nel database e restituisce i fd
 * 		degli utenti da notificare 
 * 
 * @param msg 
 * @param no_fd 
 * @param db 
 * @return int* 
 */

long *manage_postmessage(message_t *msg, int sender_fd, int *no_fd, enum operation *branch, sqlite3 *db)
{
	long receiver_fd = GETLONG_ERROR, group_id = GETLONG_ERROR;
	sqlite3_int64 chat_id = -1;

	*branch = user;
	long *fd = NULL;

	char *sender = msg->hdr.sender;
	char *receiver = msg->data.hdr.receiver;
	char *message = msg->data.buf;

	/**-----------------------------------------------------------------
	 * @brief 3 casi:
	 * 1. messaggio testuale o file a singolo utente
	 * 2. messaggio testuale o file a gruppo
	 * 3. messaggio testuale a tutti gli utenti
	 -----------------------------------------------------------------*/

	/* controllo che l'utente non voglia inviarsi il messaggio da solo */
	if (strcmp(msg->hdr.sender, msg->data.hdr.receiver) == 0)
	{
		*no_fd = -1;
		fd = NULL;
		return fd;
	}

	/**
	 * @brief prima cosa da fare: ottenere i file descriptor a cui recapitare 
	 * 		il messaggio (utenti online), altrimenti verrà solo inserito nel
	 * 		database
	 */
	/* CASO 1/2: */
	if ((msg->hdr.op == POSTTXT_OP) || (msg->hdr.op == POSTFILE_OP))
	{
		/* controllo che l'utente ricevente esista e ne prendo il fd */
		exec_getuserfd(db, receiver, &receiver_fd);

		if (receiver_fd == GETLONG_ERROR) /* l'utente non esiste: potrebbe essere un gruppo */
		{
			exec_checkexistinggroup(db, receiver, &group_id);
			if (group_id == GETLONG_ERROR) /* non esiste nemmeno il gruppo */
			{
				*no_fd = -1;
				fd = NULL;
				return fd;
			}
			else /* recupero i fd di tutti gli utenti del gruppo attualmente online */
			{
				int isInGroup = 0;

				*branch = group;

				exec_getonlineuser_in_group(db, group_id, &fd, no_fd);

				/* controllo che l'utente faccia parte del gruppo */
				for (int i = 0; i < *no_fd; i++)
					if ((fd[i]) && (fd[i] == sender_fd))
					{
						isInGroup = 1;
						break;
					}

				if (!isInGroup)
				{
					*no_fd = NOT_IN_GROUP;
					if (fd)
					{
						free(fd);
						fd = NULL;
					}
					return fd;
				}
			}
		}
		else
		{
			*branch = user;
			fd = safe_malloc(sizeof(long));
			/* da cambiare in caso di gruppi */
			*no_fd = 1;
			*fd = receiver_fd;
		}
	}
	else if (msg->hdr.op == POSTTXTALL_OP)
	{
		*branch = user;
		exec_getonlineuserfd(db, &fd, no_fd);
	}

	/** 
	 * @brief secondo: bisogna controllare che la chat sia già presente nel database
	 *			altrimenti va aggiunta 
	*/

	if (*branch == user)
	{
		char *user_list;
		int no_user;

		if (msg->hdr.op == POSTTXTALL_OP) /* POSTTXT_ALL */
		{
			exec_getallusername(db, &user_list, &no_user);
			if (no_user <= 1) // ci sei solo te nel database
			{
				return NULL; // ??
			}
		}
		else
		{
			user_list = receiver;
			no_user = 1;
		}

		for (int i = 0; i < no_user; i++)
		{
			char *curr_user = user_list + (i * (MAX_NAME_LENGTH + 1));
			/* non voglio inviarmi il messaggio da solo */
			if (strcmp(curr_user, sender) != 0)
			{
				chat_id = exec_checkexistingchat(db, sender, curr_user);
				if (chat_id == GETLONG_ERROR) /* chat non esistente */
				{
					chat_id = exec_createchat(db);
					if (chat_id != -1)
					{
						exec_insert_user_in_chat(db, sender, chat_id);
						exec_insert_user_in_chat(db, curr_user, chat_id);
					}
				}
				/* gestisco subito nel ciclo la posttxt_all */
				if (msg->hdr.op == POSTTXTALL_OP)
					exec_insertmessage(db, sender, msg->data.buf, chat_id);
			}
		} /* CASO 3: chiuso */

		if (msg->hdr.op == POSTTXTALL_OP)
			free(user_list);
	}

	/* inserisco il filename nel database e salvo il file nella cartella */
	if (msg->hdr.op == POSTFILE_OP)
	{
		char *filename = message;

		/* il buffer contiene "filename'\0'dati del file" */
		char *file_data = strchr(msg->data.buf, '\0');
		file_data++;
		char *filepath = get_filepath();

		if (*branch == group)
			exec_postfile(db, sender, filename, group_id);
		else if (*branch == user)
			exec_postfile(db, sender, filename, chat_id);

		/* salvo il file non con il suo filename ma con la sua chiave primaria */
		sqlite3_int64 save_as = sqlite3_last_insert_rowid(db);

		char *temp;
		asprintf(&temp, "%s/%lld", filepath, save_as);

		/* A  call  to creat() is equivalent to calling open() with flags equal to
       O_CREAT|O_WRONLY|O_TRUNC. */
		int file_fd = creat(temp, 0);
		fchmod(file_fd, S_IRUSR | S_IWUSR);
		if (file_fd == -1)
		{
			free(temp);
			handle_error(STRING_BAD_FILE_OPEN);
		}

		int ret = write(file_fd, file_data, msg->data.hdr.len - strlen(filename) - 1);

#ifdef MAKE_TEST_HAPPY /* salvo una copia del file con il nome originale */
		char *system_command = NULL;
		asprintf(&system_command, "cp %s %s/%s", temp, filepath, filename);
		system(system_command);
		free(system_command);
#endif

		if (ret <= 0)
		{
			free(temp);
			close(file_fd);
			handle_error(STRING_HANDLE_BAD_FILE_WRITING);
		}

		free(temp);
		close(file_fd);
	}
	else if (msg->hdr.op == POSTTXT_OP)
	{
		if (*branch == group)
			exec_insertmessage(db, sender, message, group_id);
		else if ((*branch == user) && (chat_id != -1))
			exec_insertmessage(db, sender, message, chat_id);
	}

	return fd;
}

op_t manage_getfile(message_t *msg, message_t *ans, sqlite3 *db)
{
	long id_file = GETLONG_ERROR;

	char *filename = msg->data.buf, *sender = msg->hdr.sender;
	exec_getfile(db, sender, filename, &id_file);

	/* non ci sono file con quel nome */
	if (id_file == GETLONG_ERROR)
		return OP_NO_SUCH_FILE;

	char *filepath = get_filepath();
	char *temp;

	asprintf(&temp, "%s/%ld", filepath, id_file);

	int fd = open(temp, O_RDONLY);
	/* impossibile aprire il file */
	if (fd == -1)
	{
		free(temp);
		handle_error(STRING_HANDLE_BAD_FILE_READING);
	}

	FILE *stream = fdopen(fd, "r");
	fseek(stream, 0L, SEEK_END); // posizione iniziale
	ans->data.hdr.len = ftell(stream); // leggo dimensione del file per l'allocazione
	rewind(stream);
	fclose(stream);
	ans->data.buf = safe_malloc(ans->data.hdr.len * sizeof(char));
#ifdef MAKE_VALGRIND_HAPPY
	memset(ans->data.buf, 0, ans->data.hdr.len * sizeof(char));
#endif
	read(fd, ans->data.buf, ans->data.hdr.len);
	ans->hdr.op = OP_OK;

	close(fd);

	free(temp);
	return OP_OK;
}

op_t manage_creategroup(char *group_name, char *creator, sqlite3 *db)
{
	long result = GETLONG_ERROR;
	sqlite3_int64 chat_id;

	/* controllo che non esista già un utente o gruppo con quel nome */
	exec_checkexistname(db, group_name, &result);

	if (result != 0) /* esiste già */
		return OP_NICK_ALREADY;

	/* creo il gruppo */
	chat_id = exec_creategroup(db, group_name, creator);

	if (chat_id == -1) /* errore nella creazione */
		return OP_FAIL;

	exec_insert_user_in_chat(db, creator, chat_id);
	return OP_OK;
}

op_t manage_addtogroup(char *group_name, char *user, sqlite3 *db)
{
	long chat_id;
	int query_result;

	exec_checkexistinggroup(db, group_name, &chat_id);

	if (chat_id == GETLONG_ERROR) /* gruppo non esistente */
		return OP_FAIL;

	query_result = exec_insert_user_in_chat(db, user, chat_id);
	if (query_result == SQLITE_CONSTRAINT) /* utente già nel gruppo */
		return OP_NICK_ALREADY;

	return OP_OK;
}

op_t manage_removeuserfromgroup(char *group_name, char *user, int curr_fd, sqlite3 *db)
{
	long chat_id;

	exec_checkexistinggroup(db, group_name, &chat_id);

	if (chat_id == GETLONG_ERROR) /* il gruppo non esiste */
		return OP_NICK_UNKNOWN;

	long *fd = NULL;
	int no_fd, isInGroup = 0;

	exec_getonlineuser_in_group(db, chat_id, &fd, &no_fd);
	if (no_fd <= 0)
		return OP_FAIL;

	for (int i = 0; i < no_fd; i++)
		if ((fd[i]) && (fd[i] == curr_fd)) /* l'utente è nel gruppo */
		{
			isInGroup = 1;
			break;
		}

	if (fd)
		free(fd);

	if (!isInGroup) /* l'utente non è nel gruppo */
		return OP_NICK_UNKNOWN;

	exec_removeuser_from_group(db, chat_id, user);

	return OP_OK;
}

op_t manage_deletegroup(char *group_name, char *sender, sqlite3 *db)
{
	int ret = exec_delgroup(db, sender, group_name);
	if (ret == SQLITE_OK)
		return OP_OK;
	/* else */
	return OP_FAIL;
}
int manage_getprevmsgs(char *sender, message_t **ans, sqlite3 *db)
{
	int message_number = exec_getprevmsgs(db, sender, get_maxmsgs(), ans);
	if (message_number < 0)
		return -1;
	return message_number;
}

void manage_getstats(sqlite3 *db, struct statistics *stats)
{
#ifdef MAKE_TEST_HAPPY
	pthread_mutex_lock(&access_sem_stats);
	memcpy(stats, sem_stats, sizeof(*stats));
	pthread_mutex_unlock(&access_sem_stats);
#endif
#ifndef MAKE_TEST_HAPPY
	long temp;
	exec_gettotaluser(db, &temp);
	if (temp > 0)
		stats->nusers = temp;
	exec_getnumberonlineuser(db, &temp);
	if (temp > 0)
		stats->nonline = temp;

	exec_get_from_stats(db, &(stats->nnotdelivered), &(stats->nfilenotdelivered), &(stats->ndelivered), &(stats->nfiledelivered), &(stats->nerrors));
#endif
}

const char *createdb()
{
	return query_createdb;
}

const char *cleardb()
{
	return query_cleardb;
}
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/**
 * @brief il seguente file contiene:
 * 		-l'implementazione delle interfacce di creazione/distruzione del pool di thread
 * 		- la routine di esecuzione dei thread slaves
 * 		- l'implementazione delle strutture necessarie all'invio atomico
 * 			su descrittori eventualmente in scrittura da thread diversi
 * 		- l'implementazione delle strutture necessarie all'esecuzione di 
 * 			operazioni su uno stesso utente da parte di più thread in modo
 * 			consistente (es. nessun thread può eseguire la disconnessione di
 * 									un utente se un altro thread ci sta ancora
 * 									lavorando)
 * @file slaves.c
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-08-28
 */
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

#include "utils.h"
#include "queues.h"
#include "slaves.h"
#include "message.h"
#include "ops.h"
#include "connections.h"
#include "sqlite3.h"
#include "core.h"
#include "queries.h"

#ifdef MAKE_TEST_HAPPY
pthread_mutex_t access_sem_stats = PTHREAD_MUTEX_INITIALIZER;
long sem_stats[7];

enum stat_type
{
	nusers = 0,
	nonline = 1,
	ndelivered = 2,
	nnotdelivered = 3,
	nfiledelivered = 4,
	nfilenotdelivered = 5,
	nerrors = 6
};
#endif

/**------------------------------------------------------------------------
 * @brief 	strutture e funzioni necessarie all'invio di messaggi
 *  									in modo concorrente 
 ------------------------------------------------------------------------*/
static int *writing_fd = NULL;
static int no_slaves;

static pthread_mutex_t access_writing_fd = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t busy_writing_fd = PTHREAD_COND_INITIALIZER;

/**
 * @brief garantisce che al termine dell'esecuzione il thread abbia l'accesso
 * 		in scrittura ESCLUSIVO sul descrittore fd
 * 
 * @param fd descrittore sul quale scrivere
 * @param my_id id enumerativo del thread
 */
static inline void start_safe_writing(int fd, int my_id)
{
	/**
	 * @brief controllo che su tutti gli altri thread nessuno stia già 
	 * 			lavorando sul descrittore sul quale voglio scrivere
	 */
	pthread_mutex_lock(&access_writing_fd);
	for (int i = 0; i < no_slaves; i++)
	{
		if ((i != my_id) && (writing_fd[i] == fd))
		{
			pthread_cond_wait(&busy_writing_fd, &access_writing_fd);
			i = 0; /* devo rieseguire il ciclo quando ricevo il segnale */
		}
	}
	writing_fd[my_id] = fd; /* mi imposto come scrittore su fd */
	pthread_mutex_unlock(&access_writing_fd);
}

/**
 * @brief imposta il termine dell'accesso in scrittura esclusivo da parte
 * 	 del thread sul descrittore fd
 * 
 * @param fd descrittore
 * @param my_id id enumerativo del thread
 */
static inline void stop_safe_writing(int fd, int my_id)
{
	pthread_mutex_lock(&access_writing_fd);
	writing_fd[my_id] = VOID_FD; /* libero la scrittura su fd */
	pthread_cond_broadcast(&busy_writing_fd);
	pthread_mutex_unlock(&access_writing_fd);
}

/**
 * @brief invio del messaggio "msg" sul descrittore "fd" con scrittura 
 * 			esclusiva garantita
 * 
 * @param fd descrittore in scrittura
 * @param msg messaggio da inviare
 * @param my_id id enumerativo del thread
 */
void send_message(int fd, message_t *msg, int my_id)
{
	if (msg->data.hdr.len < 0)
		msg->data.hdr.len = 0;
	start_safe_writing(fd, my_id);
	sendRequest(fd, msg);
	stop_safe_writing(fd, my_id);
}

/**
 * @brief invio esclusivo dell'header contenente l'operazione "op" al descrittore "fd"
 * 
 * @param db handler db
 * @param fd descrittore
 * @param op operazione da inviare
 * @param my_id id del thread
 */
void send_ack(sqlite3 *db, int fd, op_t op, int my_id)
{
	message_hdr_t ack;
	setHeader(&ack, op, "server");

	start_safe_writing(fd, my_id);
	sendHeader(fd, &ack);
	stop_safe_writing(fd, my_id);

	if (op != OP_OK)
	{
#ifdef MAKE_TEST_HAPPY
		increase_sem_stats(sem_stats[nerrors], 1);
#endif
#ifndef MAKE_TEST_HAPPY
		exec_increasestats(db, 0, 0, 0, 0, 1);
#endif
	}
}

/**------------------------------------------------------------------------
 * @brief strutture necessarie alla verifica della consistenza della 
 * 		attuale operazione
 ------------------------------------------------------------------------*/

typedef enum _working_operation
{
	signup,  /**< registrazione */
	generic, /**< generica operazione, es: invio messaggio, ecc. */
	ending	/**< disconnessione / unregister */
} working_operation;

/**
 * @brief struttura contenente il tipo di operazione da eseguire e 
 * 		il descrittore dal quale è richiesta
 * 
 */
typedef struct _critic_zone_entry
{
	working_operation wop;
	int fd; /**< descrittore dal quale è richiesta l'operazione */
} critic_zone_entry;

static pthread_mutex_t access_critic_zone = PTHREAD_MUTEX_INITIALIZER;
static critic_zone_entry *critic_zone = NULL;

/**
 * @brief permette di verificare la consistenza dell'operazione
 * 		verso la concorrenza degli altri thread:
 * 			- se la consistenza è garantita viene inserito nella 
 *  				struttura
 *				- ritorna 0 altrimenti
 * 
 * @param wop 
 * @param fd 
 * @return int 1 se è garantita la consistenza, 0 altrimenti
 */
static int check_op_consistence(working_operation wop, int fd, int my_id)
{
	/**
	 * @brief controllo tutti gli altri thread se stanno eseguendo un'operazione
	 * 			per conto di "fd"
	 * @warning è permesso:
	 * 			- eseguire un'operazione generica se tutti stanno eseguendo 
	 * 				operazioni generiche
	 * 			- eseguire registrazione e disconnessione se NESSUNO sta 
	 * 				eseguendo operazioni generiche
	 * 			NON è permesso:
	 * 			- eseguire operazioni generiche se è in corso la registrazione
	 * 			- eseguire la disconnessione se altri stanno eseguendo altre
	 * 				operazioni
	 * 
	 */
	pthread_mutex_lock(&access_critic_zone);
	for (int i = 0; i < no_slaves; i++)
		if ((i != my_id) && (critic_zone[i].fd == fd))
		{
			/* COND: ho trovato un altro slave che sta lavorando 
						sullo stesso utente */

			/* mi è arrivata una operazione prima che l'utente sia 
				registrato */
			if ((wop == generic) && (critic_zone[i].wop == signup))
			{
				pthread_mutex_unlock(&access_critic_zone);
				return 0;
			}
			/* voglio disconnettere l'utente mentre altri ci stanno
				ancora lavorando: non posso */
			if (wop == ending)
			{
				pthread_mutex_unlock(&access_critic_zone);
				return 0;
			}
		}

	critic_zone[my_id].wop = wop;
	critic_zone[my_id].fd = fd;
	pthread_mutex_unlock(&access_critic_zone);

	return 1; /* consistenza garantita */
}

static void clean_critic_zone(int my_id)
{
	pthread_mutex_lock(&access_critic_zone);
	critic_zone[my_id].fd = VOID_FD;
	pthread_mutex_unlock(&access_critic_zone);
}

/**------------------------------------------------------------------------
 * @brief 						 routine degli slaves
 ------------------------------------------------------------------------*/
/**
 * @brief routine principale degli slaves, ricevono il messaggio dalla
 * 		coda e eseguono l'operazione adeguata
 * @warning è qui che viene liberato il messaggio allocato dal core
 * 
 * @param arg id enumerativo del thread
 * @return void* 
 */
void *slave_routine(void *arg)
{
	int must_terminate = 0;
	unsigned int my_id = *((unsigned int *)arg);
	sqlite3 *db_handler;

	open_db(&db_handler);
	sqlite3_busy_timeout(db_handler, 1000);

	while (!must_terminate)
	{
		/* estraggo il messaggio dalla coda */
		wrapper curr_work = queue_pop();

		/* ho ricevuto il segnale di terminazione del server */
		if (curr_work.fd == TERMINATION_FD)
		{
			/* COND: il messaggio è stato inviato dal core */
			must_terminate = 1;
			free_message(curr_work.msg);
			break;
		}

		op_t op = curr_work.msg->hdr.op, result = OP_NOOP;
		working_operation wop;

		/* devo verificare che il tipo di operazione che voglio eseguire
			sia consistente */
		if (op == REGISTER_OP)
			wop = signup;
		else if ((op == UNREGISTER_OP) || (op == DISCONNECT_OP))
			wop = ending;
		else if ((op != OP_FAIL) && (op != OP_MSG_TOOLONG))
			wop = generic;

		/**
		 * @warning rimettere l'operazione in fondo alla coda
		 * 		nel caso di un altro slave che sta eseguendo una
		 * 		operazione incompatibile alla nostra non è una
		 * 		cosa molto intelligente nel caso di 2/3 messaggi in
		 * 		coda in un sistema multi-threaded. Tuttavia considerando
		 * 		le specifiche fornite dell'ordine di migliaia di utenti
		 * 		connessi contemporaneamente questa DOVREBBE essere
		 * 		una scelta maggiormente produttiva rispetto ad una wait
		 * 		del thread
		 */
		if ((op != OP_FAIL) && (op != OP_MSG_TOOLONG) &&
			 (!check_op_consistence(wop, curr_work.fd, my_id)))
		{
			/* rimetto l'operazione in fondo alla coda */
			queue_push(curr_work.msg, curr_work.fd);
			continue; /* ricomincio */
		}

		message_t ans;
		memset(&ans, 0, sizeof(message_t));

		/**
		 * @brief le prime operazioni devono inviare un messaggio se
		 * 		l'operazione è andata a buon fine, un header altrimenti
		 */
		if (op == REGISTER_OP)
		{
			result = manage_insertuser(curr_work.msg->hdr.sender, curr_work.fd, &ans, db_handler);

#ifdef MAKE_TEST_HAPPY
			manage_disconnectuser(curr_work.fd, db_handler);
			if (result == OP_OK)
				increase_sem_stats(sem_stats[nusers], 1);

			increase_sem_stats(sem_stats[nonline], 1);
#endif
		}
		else if (op == CONNECT_OP)
		{
			result = manage_connectuser(curr_work.msg->hdr.sender, curr_work.fd, &ans, db_handler);
#ifdef MAKE_TEST_HAPPY
			/* anche se la connessione non avviene il valore verrà 
				decrementato dalla disconnessione */
			increase_sem_stats(sem_stats[nonline], 1);
#endif
		}
		else if (op == USRLIST_OP)
		{
			int buf_dim;
			char *temp_buf = exec_get_online_user(db_handler, &buf_dim);
			set_reply_message(&ans, OP_OK, temp_buf, buf_dim, "", curr_work.msg->hdr.sender);
			result = OP_OK;
		}
		else if (op == GETFILE_OP)
		{
			result = manage_getfile(curr_work.msg, &ans, db_handler);
		}
		/* vengono valutate qui */
		if (result == OP_OK)
			send_message(curr_work.fd, &ans, my_id);
		else if (result != OP_NOOP)
			send_ack(db_handler, curr_work.fd, result, my_id);

		/*[!!] da qui in poi i rami gestiscono personalmente le risposte */
		else if (op == DISCONNECT_OP)
		{
			manage_disconnectuser(curr_work.fd, db_handler);
#ifdef MAKE_TEST_HAPPY
			increase_sem_stats(sem_stats[nonline], -1);
#endif
			/* You can't call close() unless you know that all other threads
			 are no longer in a position to be using that file descriptor at all.*/
			close(curr_work.fd);
		}
		else if (op == UNREGISTER_OP)
		{
			result = manage_unregisteruser(curr_work.msg->hdr.sender, db_handler);
			send_ack(db_handler, curr_work.fd, result, my_id);
#ifdef MAKE_TEST_HAPPY
			if (result == OP_OK)
				increase_sem_stats(sem_stats[nusers], -1);
#endif
		}
		/**
		 * @brief tutte le richieste di messaggi vengono valutate qui
		 * 		file/gruppi/messaggi singoli/messaggi multipli/ ecc.
		 * 
		 */
		else if ((op == POSTFILE_OP) || (op == POSTTXT_OP) || (op == POSTTXTALL_OP))
		{
			long *fd;
			int no_fd = 0;
			enum operation branch;
			
			fd = manage_postmessage(curr_work.msg, curr_work.fd, &no_fd, &branch, db_handler);
			if (no_fd > 0)
			{
				message_t notify; /* messaggio da inviare al ricevente */
				int sent_messages = 0;
				int not_sent_messages = 0;

				memcpy(&notify, curr_work.msg, sizeof(message_t));
				notify.hdr.op = (op == POSTFILE_OP) ? FILE_MESSAGE : TXT_MESSAGE;
				notify.data.buf = curr_work.msg->data.buf;
				for (int i = 0; i < no_fd; i++)
				{
					long *curr_receiver = (fd + i);

					if (!curr_receiver)
						continue;
					/**
					 * @brief devo servire la risposta a un gruppo
					 * 
					 */
					if (branch == group)
					{
						/* per un qualche motivo a me ignoto nei gruppi bisogna inviarsi i 
						messaggi da soli */
						if ((*curr_receiver != VOID_FD))
						{
							send_message(*curr_receiver, &notify, my_id);
							sent_messages++;
						}
						else if (*curr_receiver == VOID_FD)
							not_sent_messages++;
					}
					/**
					 * @brief devo servire la risposta ad un utente
					 */
					else
					{
						if ((*curr_receiver != curr_work.fd) && (*curr_receiver != VOID_FD))
						{
							send_message(*curr_receiver, &notify, my_id);
							sent_messages++;
						}
						else if (*curr_receiver == VOID_FD)
							not_sent_messages++;
					}

					/**
					 * @brief valutazione delle statistiche
					 * 
					 */
					if (op == POSTFILE_OP)
					{
#ifdef MAKE_TEST_HAPPY
						increase_sem_stats(sem_stats[nfilenotdelivered], not_sent_messages);
						increase_sem_stats(sem_stats[nfiledelivered], sent_messages);
#endif
#ifndef MAKE_TEST_HAPPY
						exec_increasestats(db_handler, 0, not_sent_messages, 0,
												 sent_messages, 0);
#endif
					}
					else
					{
#ifdef MAKE_TEST_HAPPY
						increase_sem_stats(sem_stats[nnotdelivered], not_sent_messages);
						increase_sem_stats(sem_stats[ndelivered], sent_messages);
#endif
#ifndef MAKE_TEST_HAPPY
						exec_increasestats(db_handler, not_sent_messages, 0,
												 sent_messages, 0, 0);
#endif
					}
				}

				free(fd);
			}

			/**
			 * @brief operazioni in risposta al mittente
			 * 
			 */
			if (no_fd == NOT_IN_GROUP)
				send_ack(db_handler, curr_work.fd, OP_NICK_UNKNOWN, my_id);
			else if (no_fd == -1)
				send_ack(db_handler, curr_work.fd, OP_FAIL, my_id);
			else
				send_ack(db_handler, curr_work.fd, OP_OK, my_id);
		}
		else if (op == GETPREVMSGS_OP)
		{
			message_t *list = NULL;

			ssize_t no_message = manage_getprevmsgs(curr_work.msg->hdr.sender, &list, db_handler);
			if (no_message < 0)
				send_ack(db_handler, curr_work.fd, OP_FAIL, my_id);
			else
			{
				message_t notify;
				notify.hdr.op = OP_OK;
				/* mah */
				notify.data.buf = safe_malloc(sizeof(size_t));
				memcpy(notify.data.buf, &no_message, sizeof(size_t));
				notify.data.hdr.len = sizeof(size_t);
				send_message(curr_work.fd, &notify, my_id);

				for (int i = 0; i < no_message; i++)
				{
					message_t *curr_msg = list + i;
					send_message(curr_work.fd, curr_msg, my_id);
				}
				free(notify.data.buf);
			}

			/**
			 * @brief libero la lista dei messaggi dopo l'invio
			 * 
			 */
			if (list)
			{
				for (int i = 0; i < no_message; i++)
					if (list[i].data.buf)
						free(list[i].data.buf);

				free(list);
			}
		}

		/**------------------------------------------------------------------------
		 * @brief  					parte opzionale sui gruppi
		 ------------------------------------------------------------------------*/
		else if (op == CREATEGROUP_OP)
		{
			result = manage_creategroup(curr_work.msg->data.hdr.receiver, curr_work.msg->hdr.sender, db_handler);
			send_ack(db_handler, curr_work.fd, result, my_id);
		}
		else if (op == ADDGROUP_OP)
		{
			result = manage_addtogroup(curr_work.msg->data.hdr.receiver, curr_work.msg->hdr.sender, db_handler);
			send_ack(db_handler, curr_work.fd, result, my_id);
		}
		else if (op == DELGROUP_OP)
		{
			result = manage_removeuserfromgroup(curr_work.msg->data.hdr.receiver, curr_work.msg->hdr.sender, curr_work.fd, db_handler);
			send_ack(db_handler, curr_work.fd, result, my_id);
		}
		/* task opzionale: il nome del gruppo deve essere inviato nel receiver */
		else if (op == UNREGISTER_GROUP)
		{
			result = manage_deletegroup(curr_work.msg->data.hdr.receiver, curr_work.msg->hdr.sender, db_handler);
			send_ack(db_handler, curr_work.fd, result, my_id);
		}

		//-------------------------------------------------------------------------//

		else if ((op == OP_FAIL) || (op == OP_MSG_TOOLONG))
			send_ack(db_handler, curr_work.fd, op, my_id);
		else
		{
			fprintf(stderr, "[!!] Non so gestire la richiesta %d\n", op);
			send_ack(db_handler, curr_work.fd, OP_FAIL, my_id);
			continue;
		}

		/* imposto il termine dell'operazione nella struttura */
		if ((op != OP_FAIL) && (op != OP_MSG_TOOLONG))
			clean_critic_zone(my_id);

		/* pulisco il messaggio */
		free_message(curr_work.msg);
		if (ans.data.buf)
		{
			free(ans.data.buf);
			ans.data.buf = NULL;
		}
	}

	sqlite3_close(db_handler);
	db_handler = NULL;
	free(arg);
	return (void *)0;
}

int init_slaves(int n_slaves)
{
	writing_fd = safe_malloc(n_slaves * sizeof(int));
	critic_zone = safe_malloc(n_slaves * sizeof(critic_zone_entry));

	/**
	 * @brief inizializzo le strutture condivise
	 * 
	 */
	for (int i = 0; i < n_slaves; i++)
	{
		writing_fd[i] = VOID_FD;
		critic_zone[i].fd = VOID_FD;
	}

#ifdef MAKE_TEST_HAPPY
	for (int i = 0; i < 7; i++)
		sem_stats[i] = 0;
#endif

	no_slaves = n_slaves;

	return EXIT_SUCCESS;
}

void destroy_slaves()
{
	/* risveglio eventuali slaves in coda sulla struttura
		per ottenere la scrittura esclusiva su fd: raro */
	pthread_cond_broadcast(&busy_writing_fd);

	if (writing_fd)
	{
		free(writing_fd);
		writing_fd = NULL;
	}
	if (critic_zone)
	{
		free(critic_zone);
		critic_zone = NULL;
	}

#ifdef MAKE_TEST_HAPPY
	pthread_mutex_destroy(&access_sem_stats);
#endif

	pthread_cond_destroy(&busy_writing_fd);
	pthread_mutex_destroy(&access_writing_fd);
	pthread_mutex_destroy(&access_critic_zone);
}
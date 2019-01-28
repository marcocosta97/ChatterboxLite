#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/**
 * @brief il seguente file contiene l'implementazione delle 
 * 		operazioni di push e pop per la coda di messaggi utilizzata per lo
 * 		scambio di messaggi tra core e slaves. Tutte le operazioni sono thread-safe
 * 		
 * 
 * @file queues.c
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-08-28
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/queue.h>
#include <signal.h>

#include "queues.h"
#include "utils.h"
#include "slaves.h"

/**
 * @brief variabili condivise, quali mutex, condizioni di attesa sulla coda
 * 			e condizioni di terminazione
 * 
 */
static pthread_mutex_t mux = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pop_waiting = PTHREAD_COND_INITIALIZER;
static volatile sig_atomic_t isFree = 0;
static volatile sig_atomic_t must_terminate = 0;
static int isInit = 0;

/**
 * @brief inizializza la coda tramite la macro STAILQ_INIT
 * 
 */
void queue_init()
{
	if (!isInit)
	{
		STAILQ_INIT(&head);
		isInit = 1;
	}
}

/**
 * @brief inserisce un nuovo messaggio in fondo alla coda
 * @note STAILQ_INSERT_TAIL è O(1)
 * 
 * @param msg messaggio da inserire
 * @param fd descrittore di provenienza
 */
void queue_push(message_t *msg, int fd)
{
	job_entry *job = safe_malloc(sizeof(job_entry));
	(*job).msg = msg;
	(*job).fd = fd;

	pthread_mutex_lock(&mux);
	STAILQ_INSERT_TAIL(&head, job, next_entry);
	pthread_cond_signal(&pop_waiting);
	pthread_mutex_unlock(&mux);
}

/**
 * @brief estrazione di un messaggio dalla coda
 * 
 * @return wrapper struttura wrapper di ritorno contenente messaggio e descr
 */
wrapper queue_pop()
{
	wrapper w;
	message_t *msg;
	job_entry *job;

	/* questo controllo evita che possa essere stato distrutto il mutex prima di poter controllare
		la variabile di terminazione */
	if (must_terminate)
	{
		msg = safe_malloc(sizeof(message_t));
		msg->hdr.op = OP_NOOP; /* operazione gestita come terminazione */
		msg->data.buf = NULL;
		w.msg = msg;
		w.fd = TERMINATION_FD; /* imposto un descrittore fasullo */
		return w;
	}

	pthread_mutex_lock(&mux);
	while ((STAILQ_EMPTY(&head)) && (!must_terminate))
		pthread_cond_wait(&pop_waiting, &mux);
	if (must_terminate) /* mi è stato inviato il segnale di terminazione, non un messaggio */
	{
		pthread_mutex_unlock(&mux);
		msg = safe_malloc(sizeof(message_t));
		msg->hdr.op = OP_NOOP;
		msg->data.buf = NULL;
		w.msg = msg;
		w.fd = TERMINATION_FD; /* imposto un descrittore fasullo */
		return w;
	}
	job = STAILQ_FIRST(&head);
	STAILQ_REMOVE_HEAD(&head, next_entry); /* O(1) rimozione in testa */
	pthread_mutex_unlock(&mux);
	w.msg = job->msg;
	w.fd = job->fd;
	free(job);

	return w;
}

/**
 * @brief setta a 1 la variabile condivisa di terminazione, 
 * 		sveglia tutti i thread in wait e pulisce i messaggi ancora in coda
 * 
 */
void queue_free()
{
	must_terminate = 1;
	pthread_cond_broadcast(&pop_waiting);

	if (!isFree)
	{
		pthread_mutex_lock(&mux);
		while (!STAILQ_EMPTY(&head))
		{
			job_entry *job = STAILQ_FIRST(&head);
			STAILQ_REMOVE_HEAD(&head, next_entry);
			/* libero il messaggio rimasto in coda */
			free_message(job->msg);
			free(job);
		}
		pthread_mutex_unlock(&mux);

		isFree = 1;
	}

	pthread_cond_broadcast(&pop_waiting);
}

void destroy_queue_mutex()
{
	pthread_mutex_destroy(&mux);
	pthread_cond_destroy(&pop_waiting);
}

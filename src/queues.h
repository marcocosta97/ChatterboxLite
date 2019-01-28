/**
 * @brief fornisce astrazione  alla componente CORE e ai 
 * relativi slave circa l'utilizzo delle code di comunicazione
 * 
 * NOTA: l'implementazione DEVE essere atomica
 * 
 * @file queues.h
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-07-10
 */
#ifndef _QUEUES_H_
#define _QUEUES_H_

#include <sys/queue.h>
#include "message.h"

/*
	A singly-linked tail queue	is headed by a structure defined by the
     STAILQ_HEAD macro.	 This structure	contains a pair	of pointers, one to
     the first element in the tail queue and the other to the last element in
     the tail queue.  The elements are singly linked for minimum space and
     pointer manipulation overhead at the expense of O(n) removal for arbi-
     trary elements.  New elements can be added	to the tail queue after	an
     existing element, at the head of the tail queue, or at the	end of the
     tail queue.

	  da "man queue"
*/
static STAILQ_HEAD(job_queue, job_entry) head = STAILQ_HEAD_INITIALIZER(head);

/**
 * @brief struttura wrapper per la restituzione dei parametri
 * 
 */
typedef struct wrapper
{
   message_t *msg;
   int fd;
} wrapper;

/**
 * @brief entry della coda
 * 
 */
typedef struct job_entry
{
   message_t *msg; /* il messaggio */
   int fd;         /* il descrittore dal quale è arrivato il messaggio */
   STAILQ_ENTRY(job_entry)
   next_entry; /* macro che gestisce il parametro successivo */
} job_entry;

/**
 * @brief inizializza la coda di comunicazione CORE -> SLAVE
 * 
 */
void queue_init();

/**
 * @brief effettua l'operazione di push (atomica)
 * 
 * @note STAILQ garantisce l'operazione con costo O(1)
 * @param __msg il messaggio da inserire
 * @param __fd il descrittore dal quale è arrivato il messaggio
 */
void queue_push(message_t *__msg, int __fd);

/**
 * @brief effettua l'operazione di pop e rimozione dell'elemento(atomica)
 * 
 * @note STAILQ garantisce l'operazione con costo O(1)
 * @return wrapper struttura contenente messaggio e descrittore
 */
wrapper queue_pop();

/**
 * @brief libera la restante memoria allocata dalla coda in caso 
 * di interruzione improvvisa
 * 
 */
void queue_free();

/**
 * @brief distrugge i mutex e le variabili condizione istanziate
 * @warning da chiamare dopo aver terminato tutti gli slave
 * 
 */
void destroy_queue_mutex();

#endif
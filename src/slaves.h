/**
 * @brief il seguente file contiene gli header necessari all'esecuzione e alla
 * 		creazione/distruzione delle risorse necessarie al pool di thread
 * 		slaves
 * 
 * @file slaves.h
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-07-18
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */
#ifndef _SLAVES_H_
#define _SLAVES_H_

#define SLAVE_NAME "SLAVE_"

/* indica il valore "fasullo" di fd riconosciuto come terminazione dell'operazione */
#define TERMINATION_FD -2
/* indica il valore "fasullo" di fd riconosciuto come nessuna operazione in esecuzione */
#define VOID_FD -1

#ifdef MAKE_TEST_HAPPY
#include <pthread.h>

/**
 * @brief per evitare inconsistenze sulle statistiche nell'esecuzione dei test
 *  	utilizziamo una struttura dati condivisa (più veloce) piuttosto che il
 * 	database  
 * 
 */
extern pthread_mutex_t access_sem_stats;
extern long sem_stats[7];

#define increase_sem_stats(stats, n)           \
	do                                          \
	{                                           \
		pthread_mutex_lock(&access_sem_stats);   \
		(stats) += n;                            \
		pthread_mutex_unlock(&access_sem_stats); \
	} while (0);

#endif

#include <unistd.h>

/**
 * @brief routine dei thread slave, si occupa di ottenere un messaggio dalla
 * 			struttura condivisa, eseguire le query e rispondere al client
 * 
 * @param arg indice del thread
 * @return void* 
 */
void *slave_routine(void *__arg);

/**
 * @brief inizializza le strutture condivise e i mutex necessarie alla
 * 		sincronizzazione dei pool di thread
 * @warning deve essere eseguita UNA volta sola PRIMA della creazione degli
 * 			slaves
 * 
 * @param __n_slaves numero di thread slave
 */
int init_slaves(int __n_slaves);

/**
 * @brief libera la memoria (strutture dati/mutex/ecc) allocate agli slaves
 * 
 */
void destroy_slaves();

#endif
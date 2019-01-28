/**
 * @brief file di intestazione del sorgente core.c, contiene alcuni
 * 		define per l'accesso al database
 * 
 * @file core.h
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-07-10
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */

#ifndef _CORE_H_
#define _CORE_H_

#define CORE_NAME "CORE"

#include "sqlite3.h"

#ifndef DB_NAME
#define DB_NAME "/tmp/chatterboxdb"
#endif

/**
 * @brief modalità di apertura del database, si veda la relazione
 * 
 */
#ifndef WAL_MODE
#define JOURNAL_MODE "MEMORY"
#endif
#ifdef WAL_MODE
#define JOURNAL_MODE "WAL"
#endif

/**
 * @brief restituisce un handler al database
 * 
 * @param db 
 * @return int 
 */
int open_db(sqlite3 **db);

#include "utils.h"

void stop_server();
/**
 * @brief routine principale del server, inizializza le
 * risorse neccessarie, dopodiché riceve i messaggi 
 * e li instrada verso i thread slave
 * 
 * @param conf la struttura di configurazione
 * @return int EXIT_SUCCESS, else EXIT_FAILURE
 */
int start_core(conf_param *conf, int efd);

/**
 * @brief chiude le comunicazioni client -> server e 
 * core -> slave
 * 
 */
void stop_core();

#include "stats.h"

/**
 * @brief restituisce la statistiche correnti del server
 * 
 * @return struct statistics 
 */
struct statistics retrieve_stats();

/**
 * @brief restituisce il filepath dove memorizzare i file
 * 
 * @return char* 
 */
char *get_filepath();

/**
 * @brief restituisce il numero di messaggi massimi nella history
 * 
 * @return int 
 */
int get_maxmsgs();

/**
 * @brief restituisce la path del file dove inserire le statistiche
 * 
 * @return char* 
 */
char *get_statsfile();

#endif
/**
 * @brief il seguente file contiene le dichiarazioni delle funzioni necessarie
 * 		al parsing del file (oltre che alla struttura contenente il parsing)
 * 		e la dichiarazione di alcune funzioni/define di utilità generica
 * 
 * @file utils.h
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-07-10
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */

#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

/**************************************************************************************************
 * 										FUNZIONI/DEFINE PER IL FILE DI CONF
 **************************************************************************************************/

/**
 * @brief struttura contenente i dati di configurazione di default
 * se non dichiarati nel file di configurazione verranno usati i parametri di default
 * 
 */
struct conf_param_s
{
	char *unix_path;
	unsigned int max_connections;
	unsigned int threads_in_pool;
	unsigned int max_msg_size;
	unsigned int max_file_size;
	unsigned int max_hist_msg;
	char *dir_name;
	char *stat_filename;
};

typedef struct conf_param_s conf_param;

#include "config.h"

#define CONF_PARAM_DEFAULT DEFAULT_UNIX_PATH, DEFAULT_MAX_CONNECTIONS,    \
									DEFAULT_THREADS_IN_POOL, DEFAULT_MAX_MSG_SIZE, \
									DEFAULT_MAX_FILE_SIZE, DEFAULT_MAX_HIST_MSG,   \
									DEFAULT_DIR_NAME, DEFAULT_STAT_FILENAME

/**
 * @brief inizializza la struttura allocata dinamicamente con i valori di default
 * 
 * @param dest struttura da inizializzare alloc'd (!= NULL)
 */
void init_default_conf(conf_param **dest);

/**
 * @brief parsing del file di configurazione
 * 
 * @param f file di configurazione
 * @return struct conf_param 
 */
conf_param *parseconf(FILE *f);

/**
 * @brief free di tutti i campi interni alla struttura di configurazione e free della stessa
 * 
 * @param c struttura di configurazione
 */
void free_conf_param(conf_param *c);

/**
 * @brief rimuove caratteri non ascii e spazi dal buffer
 * 
 * @param buffer 
 */
void format_string(char *buffer);

/**************************************************************************************************
 * 										FUNZIONI/DEFINE DI UTILITÀ GENERICA
 **************************************************************************************************/
#include "mystring.h"

/**
 * @brief restituisce un puntatore alla zona di memoria allocata o stampa 
 * 			un errore e esce se non vi è più spazio disponibile
 * 
 * @param n dimensione da allocare
 * @return void* puntatore alla zona allocata
 */
static inline void *safe_malloc(size_t n)
{
	void *p = malloc(n);
	if (!p && n > 0)
	{
		fprintf(stderr, STRING_BAD_MALLOC);
		exit(EXIT_FAILURE);
	}
	return p;
}

/**
 * @brief realloca la zona di memoria SENZA CONSERVARE I PRECEDENTI DATI
 * 		- old_len alla fine dell'esecuzione conterrà la nuova dimensione 
 * @warning FUNZIONA SOLO CON CHAR*
 * 
 * @param p puntatore alla vecchia zona di memoria
 * @param old_len puntatore alla vecchia dimensione
 * @param new_len nuova dimensione
 * @return void* zona di memoria allocata
 */
void *safe_c_realloc(void *p, int *old_len, int new_len);

/**
 * @brief restituisce il numero di cifre dal quale è composto il numero
 * 	(utile per allocare la dimensione esatta in una stringa)
 * 
 * @param x il numero intero
 * @return int il numero di cifre
 */
int get_digits_number(int x);

#define ERROR_NULL(string)     \
	do                          \
	{                           \
		fprintf(stderr, string); \
		exit(EXIT_FAILURE);      \
	} while (0);

#define ERROR_REALLOC ERROR_NULL(STRING_BAD_REALLOC)

#define handle_error(err)         \
	do                             \
	{                              \
		perror(STRING_PERROR(err)); \
		exit(EXIT_FAILURE);         \
	} while (0);

#define BAD_QUERY(err)                        \
	do                                         \
	{                                          \
		fprintf(stderr, STRING_BAD_QUERY, err); \
		sqlite3_free(err);                      \
	} while (0);
#endif
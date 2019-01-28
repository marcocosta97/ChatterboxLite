#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/**
 * @brief il seguente file contiene tutte le interfacce per 
 * 	l'esecuzione delle singole query sul database oltre che 
 * 	l'header delle funzioni del file queries.c
 * 
 * @file queries.h
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-05-16
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>

#include "sqlite3.h"
#include "utils.h"
#include "message.h"
#include "stats.h"

#ifndef _QUERIES_H_
#define _QUERIES_H_

#define DISCONNECTED_FD -1
#define UNEXISTING_FD -2

#define QUOTE(...) #__VA_ARGS__

/**
 * @brief wrapper per la restituzione di parametri di tipo vettore di stringhe 
 * 	da parte del database
 * 
 */
struct callback_param_string
{
	char *result;
	unsigned int curr_pos;
	long size;
};

/**
 * @brief wrapper per la restituzione di parametri di tipo vettore di long 
 * 	da parte del database
 * 
 */
struct callback_param_long
{
	long *result;
	unsigned int curr_pos;
	long size;
};

/**
 * @brief wrapper per la restituzione di parametri di tipo vettore di messaggi 
 * 	da parte del database
 * 
 */
struct callback_param_message
{
	message_t *result;
	unsigned int curr_pos;
	long size;
};

/**
 * @brief costruttore di liste di tipo generico (string, long, message)
 * 
 */
#define init_list_callback(type, name) \
	struct callback_param_##type(name); \
	(name).curr_pos = 0;                \
	(name).result = NULL;

//-------------------------------------------------------------------------//

/**
 * @brief avvisa le connessione in coda sul database che è ora di terminare
 * 
 */
void terminate_db();

/**
 * @brief esegue una generica query "query" ed è responsabile della 
 * 	sincronizzazione tra le varie operazioni simultanee
 * 
 * @param db handler del database
 * @param query la query in formato stringa
 * @param callback la funzione di callback (eseguita per ogni riga risultato)
 * @param result parametro risultato della query
 * @return int (SQLITE_OK) query eseguita correttamente
 * 				(SQLITE_CONSTRAINT) impossibile eseguire la query per 
 * 											contraddizione su un vincolo
 * 											(es. chiave primaria già presente)
 */
int exec_query(sqlite3 *db, char *query, int(callback)(void *, int, char **, char **), void *result);

/**
 * @brief funzione di callback per risultati di tipo vettore di liste
 * 
 * @param param wrapper di tipo callback_param_list
 * @param argc numero di colonne risultanti
 * @param argv vettore riga del database
 * @param col_name vettore nome delle colonne 
 * @return int (EXIT_SUCCESS) operazione ok
 * 				(EXIT_FAILURE) errore imprevisto => terminare la query
 */
int getstringlist_callback(void *param, int argc, char **argv, char **col_name);

#define GETLONG_ERROR LONG_MIN
/**
 * @brief funzione di callback per risultati di tipo  long
 * 
 * @param param puntatore a long
 * @param argc numero di colonne risultanti
 * @param argv vettore riga del database
 * @param col_name vettore nome delle colonne 
 * @return int (EXIT_SUCCESS) operazione ok
 * 				(EXIT_FAILURE) errore imprevisto => terminare la query
 */
int getlong_callback(void *param, int argc, char **argv, char **col_name);

/**
 * @brief funzione di callback per risultati di tipo vettore di long
 * 
 * @param param wrapper di tipo callback_param_long
 * @param argc numero di colonne risultanti
 * @param argv vettore riga del database
 * @param col_name vettore nome delle colonne 
 * @return int (EXIT_SUCCESS) operazione ok
 * 				(EXIT_FAILURE) errore imprevisto => terminare la query
 */
int getlonglist_callback(void *param, int argc, char **argv, char **col_name);

/**
 * @brief funzione di callback per risultati di tipo vettore di messaggi
 * 
 * @param param wrapper di tipo callback_param_message
 * @param argc numero di colonne risultanti
 * @param argv vettore riga del database
 * @param col_name vettore nome delle colonne 
 * @return int (EXIT_SUCCESS) operazione ok
 * 				(EXIT_FAILURE) errore imprevisto => terminare la query
 */
int getmessagelist_callback(void *param, int argc, char **argv, char **col_name);

#ifndef MAKE_TEST_HAPPY
/**
 * @brief funzione di callback per risultati di tipo vettore di statistiche
 * 
 * @param param vettore di long 
 * @param argc numero di colonne risultanti
 * @param argv vettore riga del database
 * @param col_name vettore nome delle colonne 
 * @return int (EXIT_SUCCESS) operazione ok
 * 				(EXIT_FAILURE) errore imprevisto => terminare la query
 */
int getstats_callback(void *param, int argc, char **argv, char **col_name);
#endif
//-------------------------------------------------------------------------//

/**
 * @brief costruisce la query in formato stringa da eseguire sul database
 * 
 * @warning asprintf alloca la stringa p con la dimensione necessaria
 */

#define fill_query(p, ...)                     \
	do                                          \
	{                                           \
		if (asprintf(&(p), ##__VA_ARGS__) == -1) \
		{                                        \
			fprintf(stderr, STRING_BAD_MALLOC);   \
			exit(EXIT_FAILURE);                   \
		}                                        \
	} while (0);

/**
 * @brief costruttore generico per una query "query_name"
 * 	prende come parametro i parametri della query ORDINATI
 * @note la query è inizializzata alla variabile "q"
 */
#define init_param(query_name, ...) \
	char *q;                         \
	fill_##query_name(q, __VA_ARGS__);

/**
 * @brief distruttore generico, da richiamare DOPO l'esecuzione della query
 * 
 */
#define destroy_param \
	free(q);
//-------------------------------------------------------------------------//

/**
 * @warning tutte le interfacce exec_nomequery forniscono una astrazione
 * 			sull'esecuzione della singola query in modo da non doversi
 * 			preoccupare di allocazione, overflow, connessione con il db, ecc.
 * 			tuttavia per avere un controllo TOTALE sulla correttezza
 * 			dell'operazione è necessario richiamarle tramite la 
 * 			interfaccia manage_nomeoperazione
 * 
 * @warning poiché le exec_nomequery contengono poche istruzioni è meglio cercare di 
 * 			ottimizzarle come inline
 * 
 * @warning So if the coder wants to supply the inline optimization hint in
 *  	GNU C, then static inline is required. Since static inline works in both 
 * 	ISO C and GNU C.
 * 
 * @warning le query sono in ordine "abbastanza" sparso, molte sono scritte
 * 			in un ordine basato sulla necessità di programmazione
 * 
 */
//-------------------------------------------------------------------------//

#define query_insertuser \
	"INSERT INTO _User "  \
	"VALUES('%s', '%d');"

#define fill_insertuser(p, user, fd) \
	fill_query(p, query_insertuser, user, fd)

/**
 * @brief inserisce l'utente "user" attualmente attivo sul file descriptor
 * 		"fd" nel database
 * 
 * @param db handler del database
 * @param user utente da inserire
 * @param fd file descriptor sul qual è attualmente attivo
 * @return int (SQLITE_OK) utente correttamente inserito
 * 				(SQLITE_CONSTRAINT) username già presente nel database
 */
static inline int exec_insertuser(sqlite3 *db, char *user, int fd)
{
	init_param(insertuser, user, fd);
	int val = exec_query(db, q, NULL, NULL);
	destroy_param;
	return val;
}
//-------------------------------------------------------------------------//

#define query_removeuser            \
	"DELETE FROM _User "             \
	"WHERE username = '%s'; "        \
	"DELETE FROM _Chat_User "        \
	"WHERE username = '%s'; "        \
	"UPDATE _Message "               \
	"SET sent_by = '#deleted_user' " \
	"WHERE sent_by = '%s';"

#define fill_removeuser(p, user) \
	fill_query(p, query_removeuser, user, user, user)

/**
 * @brief rimuove l'utente "user" e tutte le sue chat dal database 			
 * @warning i messaggi rimangono conservati nel database anche se un utente
 * 			della chat viene eliminato, di modo che gli utenti ancora 
 * 			registrati possano comunque recuperare i messaggi a loro inviati:
 * 			* in questo caso il mittente viene marcato come "#deleted_user"
 * 
 * @param db handler del database
 * @param user utente da rimuovere
 * @return int (SQLITE_OK) utente correttamente inserito
 * 				(SQLITE_CONSTRAINT) username non presente nel database
 */
static inline int exec_removeuser(sqlite3 *db, char *user)
{
	init_param(removeuser, user);
	int val = exec_query(db, q, NULL, NULL);
	destroy_param;
	return val;
}

//-------------------------------------------------------------------------//

#define query_checkexistuser \
	"SELECT COUNT(username) " \
	"FROM _User "             \
	"WHERE _User.username = '%s';"

#define fill_checkexistuser(p, user) \
	fill_query(p, query_checkexistuser, user)

/**
 * @brief controlla la presenza dell'utente "user" come registrato nel database
 * 
 * @param db handler del database
 * @param user nome utente
 * @param result (1) se è presente (0) altrimenti
 */
static inline void exec_checkexistuser(sqlite3 *db, char *user, long *result)
{
	init_param(checkexistuser, user);
	exec_query(db, q, getlong_callback, result);
	destroy_param;
}

//-------------------------------------------------------------------------//

#define query_checkexistname      \
	"SELECT COUNT(*) "             \
	"FROM _User, _Chat "           \
	"WHERE _User.username = '%s' " \
	"OR _Chat.chat_name = '%s';"

#define fill_checkexistname(p, user) \
	fill_query(p, query_checkexistname, name, name)

/**
 * @brief controlla che la stringa "name" non sia già registrata come utente
 * 		o gruppo
 * @warning non è possibile registrare un gruppo con lo stesso nome di un
 * 			utente e viceversa
 * 
 * @param db handler del database
 * @param name 
 * @param result (1) se è presente (0) altrimenti
 */
static inline void exec_checkexistname(sqlite3 *db, char *name, long *result)
{
	init_param(checkexistname, name);
	exec_query(db, q, getlong_callback, result);
	destroy_param;
}

//-------------------------------------------------------------------------//

#define query_getuserfd \
	"SELECT curr_fd "    \
	"FROM _User "        \
	"WHERE username = '%s';"

#define fill_getuserfd(p, user) \
	fill_query(p, query_getuserfd, user);

/**
 * @brief restituisce l'attuale file descriptor sul quale è collegato "user"
 * 
 * @param db handler db
 * @param user nome utente
 * @param result ([fd]) se l'utente è connesso 
 * 					(VOID_FD) se non è connesso
 * 					(GETLONG_ERROR) se l'utente non esiste  
 * @return int (SQLITE_OK) | (SQLITE_CONSTRAINT)
 */
static inline int exec_getuserfd(sqlite3 *db, char *user, long *result)
{
	init_param(getuserfd, user);
	int val = exec_query(db, q, getlong_callback, result);
	destroy_param;
	return val;
}

//-------------------------------------------------------------------------//

#define query_checkexistinggroup \
	"SELECT chat_id "             \
	"FROM _Chat "                 \
	"WHERE chat_name = '%s'; "

#define fill_checkexistinggroup(p, group_name) \
	fill_query(p, query_checkexistinggroup, group_name)

/**
 * @brief restituisce l'id (PK) della chat del gruppo di nome "group_name"
 * 
 * @param db handler db
 * @param group_name nome del gruppo
 * @param result (chat_id) se op ok, (GETLONG_ERROR) se il gruppo non esiste
 * @return int (SQLITE_OK) | (SQLITE_CONSTRAINT)
 */
static inline int exec_checkexistinggroup(sqlite3 *db, char *group_name, long *result)
{
	*result = GETLONG_ERROR;
	init_param(checkexistinggroup, group_name);
	int val = exec_query(db, q, getlong_callback, result);
	destroy_param;
	return val;
}

//-------------------------------------------------------------------------//

#define query_connectuser \
	"UPDATE _User "        \
	"SET curr_fd = %d "    \
	"WHERE username = '%s' AND curr_fd = -1;"

#define fill_connectuser(p, user, fd) \
	fill_query(p, query_connectuser, fd, user)

/**
 * @brief esegue la connessione dell'utente disconnesso "user" sul suo 
 * 		file descriptor corrente
 * @warning è garantito che un utente già connesso non possa essere 
 * 			connesso una seconda volta finché non si disconnette
 * 
 * @param db handler db
 * @param user username
 * @param fd file descriptor
 * @return int (SQLITE_OK) | (SQLITE_CONSTRAINT)
 */
static inline int exec_connectuser(sqlite3 *db, char *user, int fd)
{
	init_param(connectuser, user, fd);
	int val = exec_query(db, q, NULL, NULL);
	destroy_param;
	return val;
}

//-------------------------------------------------------------------------//

#define query_disconnectuser \
	"UPDATE _User "           \
	"SET curr_fd = -1 "       \
	"WHERE curr_fd = %d;"

#define fill_disconnectuser(p, fd) \
	fill_query(p, query_disconnectuser, fd)

/**
 * @brief imposta l'utente attualmente connesso sul file descriptor "fd"
 * 		come non connesso
 * @warning non è necessario conoscere il nome utente associato in quanto
 * 			è vincolante la relazione 1:1 tra username e fd
 * 
 * @param db handler db
 * @param fd file descriptor
 */
static inline void exec_disconnectuser(sqlite3 *db, int fd)
{
	init_param(disconnectuser, fd);
	exec_query(db, q, NULL, NULL);
	destroy_param;
}

//-------------------------------------------------------------------------//

#define query_createchat            \
	"INSERT INTO _Chat (chat_name) " \
	"VALUES(NULL);"

/**
 * @brief crea un chat tra due utenti (NON GRUPPO) e ne restituisce l'id (PK)
 * @warning chat_id è auto-incrementale
 * 
 * @param db handler db
 * @return sqlite3_int64 (-1) se non è stato possibile creare la chat
 * 							(chat_id) altrimenti 
 */
static inline sqlite3_int64 exec_createchat(sqlite3 *db)
{
	int ret = exec_query(db, query_createchat, NULL, NULL);
	return (ret != SQLITE_OK) ? -1 : sqlite3_last_insert_rowid(db);
}

//-------------------------------------------------------------------------//

#define query_creategroup                    \
	"INSERT INTO _Chat (chat_name, creator) " \
	"VALUES('%s', '%s');"

#define fill_creategroup(p, group_name, creator) \
	fill_query(p, query_creategroup, group_name, creator)

/**
 * @brief crea un gruppo di nome "group_name" e imposta "creator" come creatore
 * @warning se ne salva il creatore in quanto è l'unico a poterlo eliminare
 * 
 * @param db handler db
 * @param group_name nome del gruppo
 * @param creator creatore del gruppo
 * @return sqlite3_int64 (chat_id) se creazione ok, (-1) altrimenti
 */
static inline sqlite3_int64 exec_creategroup(sqlite3 *db, char *group_name, char *creator)
{
	init_param(creategroup, group_name, creator);
	int ret = exec_query(db, q, NULL, NULL);
	destroy_param;
	return (ret != SQLITE_OK) ? -1 : sqlite3_last_insert_rowid(db);
}

//-------------------------------------------------------------------------//

#define query_insert_user_in_chat \
	"INSERT INTO _Chat_User "      \
	"VALUES(%lld, '%s');"

#define fill_insert_user_in_chat(p, user, key) \
	fill_query(p, query_insert_user_in_chat, key, user);

/**
 * @brief inserisce l'utente "user" nella chat con PK "chat_id"
 * @warning vale sia per chat utente e gruppi
 * 
 * @param db handler db
 * @param user utente da inserire
 * @param chat_id id della chat
 * @return int (SQLITE_OK) | (SQLITE_CONSTRAINT)
 */
static inline int exec_insert_user_in_chat(sqlite3 *db, char *user, sqlite3_int64 chat_id)
{
	init_param(insert_user_in_chat, user, chat_id);
	int val = exec_query(db, q, NULL, NULL);
	destroy_param;
	return val;
}

//-------------------------------------------------------------------------//

#define query_checkexistingchat      \
	"SELECT T1.chat_id "              \
	"FROM "                           \
	"(SELECT chat_id "                \
	"FROM _Chat_User "                \
	"WHERE username = '%s') AS T1, "  \
	"(SELECT chat_id "                \
	"FROM _Chat_User "                \
	"WHERE username = '%s') AS T2, "  \
	"_Chat "                          \
	"WHERE T1.chat_id = T2.chat_id "  \
	"AND T1.chat_id = _Chat.chat_id " \
	"AND _Chat.chat_name IS NULL;" /* garantisce che non sia un gruppo */

#define fill_checkexistingchat(p, user1, user2) \
	fill_query(p, query_checkexistingchat, user1, user2)

/**
 * @brief controlla che sia già presente la chat tra "user1" e "user2"
 * @warning chat tra due utenti, non gruppo
 *  
 * @param db handler db
 * @param user1 utente1
 * @param user2 utente2
 * @return sqlite3_int64 (chat_id) se la chat esiste
 * 							 (GETLONG_ERROR) se la chat non esiste
 */
static inline sqlite3_int64 exec_checkexistingchat(sqlite3 *db, char *user1, char *user2)
{
	long result = GETLONG_ERROR;
	init_param(checkexistingchat, user1, user2);
	exec_query(db, q, getlong_callback, &result);
	destroy_param;
	return (sqlite3_int64)result;
}

//-------------------------------------------------------------------------//

#define query_insertmessage                  \
	"INSERT INTO _Message "                   \
	"(message, sent_by, chat_id, sent_time) " \
	"VALUES('%s', '%s', '%lld', datetime('now'));"

#define fill_insertmessage(p, sender, message, id) \
	fill_query(p, query_insertmessage, message, sender, id)

/**
 * @brief inserisce il messaggio "message" inviato da "sender" nella chat con id
 * 		"chat_id"
 * @warning REQUIRES: la chat deve esistere
 * 
 * @param db handler db
 * @param sender mittente
 * @param message messaggio
 * @param chat_id id della chat
 */
static inline void exec_insertmessage(sqlite3 *db, char *sender, char *message, sqlite3_int64 chat_id)
{
	init_param(insertmessage, sender, message, chat_id);
	exec_query(db, q, NULL, NULL);
	destroy_param;
}
//-------------------------------------------------------------------------//

#define query_postfile                        \
	"INSERT INTO _Message "                    \
	"(filename, sent_by, chat_id, sent_time) " \
	"VALUES('%s', '%s', '%lld', datetime('now'));"

#define fill_postfile(p, sender, filename, chat_id) \
	fill_query(p, query_postfile, filename, sender, chat_id)

/**
 * @brief inserisce il file "filename" inviato da "sender" nella chat
 * 		"chat_id"
 * @warning non viene inserito il messaggio inteso come il suo contenuto ma ne
 * 			viene inserito il filename a cui è associata una chiave primaria
 * 			con la quale viene salvato il file all'interno di DirName
 * 
 * @param db handler db
 * @param sender mittente
 * @param filename nome del file
 * @param chat_id id della chat (utente o gruppo)
 */
static inline void exec_postfile(sqlite3 *db, char *sender, char *filename, sqlite3_int64 chat_id)
{
	init_param(postfile, sender, filename, chat_id);
	exec_query(db, q, NULL, NULL);
	destroy_param;
}
//-------------------------------------------------------------------------//

/* bisogna fare i conti col fatto che il client non mi invia il mittente del file */
#define query_getfile                           \
	"SELECT message_id "                         \
	"FROM _Message, _Chat_User "                 \
	"WHERE _Chat_User.username = '%s' "          \
	"AND filename = '%s' "                       \
	"AND _Message.chat_id = _Chat_User.chat_id " \
	"ORDER BY sent_time DESC "                   \
	"LIMIT 1;" /* mi inviano più file sulla stessa chat con lo stesso nome => scarico solo l'ultimo */

#define fill_getfile(p, username, filename) \
	fill_query(p, query_getfile, username, filename)

/**
 * @brief restituisce la chiave primaria alla quale è associato il file 
 * 		"filename" inviato all'utente "username" (chat utente o gruppo)
 * 
 * @param db handler db
 * @param username destinatario
 * @param filename nome del file che devo recuperare
 * @param result chiave primaria alla quale è associato il file
 */
static inline void exec_getfile(sqlite3 *db, char *username, char *filename, long *result)
{
	init_param(getfile, username, filename);
	exec_query(db, q, getlong_callback, result);
	destroy_param;
}
//-------------------------------------------------------------------------//

#define query_getnumberonlineuser \
	"SELECT COUNT(username) "      \
	"FROM _User "                  \
	"WHERE curr_fd >= 0; "

/**
 * @brief fornisce il numero di utenti attualmente online
 * 
 * @param db handler db
 * @param result numero di utenti online
 */
static inline void exec_getnumberonlineuser(sqlite3 *db, long *result)
{
	exec_query(db, query_getnumberonlineuser, getlong_callback, result);
}

//-------------------------------------------------------------------------//

#define query_get_online_user \
	"SELECT username "         \
	"FROM _User "              \
	"WHERE curr_fd >= 0 ORDER BY username ASC;"

/**
 * @brief restituisce la lista degli utenti attualmente online
 * 		ordinata in ordine alfabetico
 * 
 * @param db handler del db
 * @param buf_dim posizioni buffer risultato
 * @return char* lista di utenti online | NULL in caso di errore 
 */
static inline char *exec_get_online_user(sqlite3 *db, int *buf_dim)
{
	init_list_callback(string, result_query);

	exec_getnumberonlineuser(db, &(result_query.size));
	if ((result_query.size == GETLONG_ERROR) || (result_query.size > INT_MAX))
	{
		*buf_dim = 0;
		return NULL;
	}

	*buf_dim = result_query.size * (MAX_NAME_LENGTH + 1);
	result_query.result = safe_malloc(*buf_dim * sizeof(char));
	memset(result_query.result, 0, *buf_dim * sizeof(char));

	exec_query(db, query_get_online_user, getstringlist_callback, &result_query);
	return result_query.result;
}
//-------------------------------------------------------------------------//

#define query_getonlineuserfd \
	"SELECT curr_fd "          \
	"FROM _User "              \
	"WHERE curr_fd >= 0; "

/**
 * @brief restituisce la lista dei descrittori associati a tutti gli 
 * 			utenti attualmente online dentro "result" e il numero di utenti
 * 			attualmente online in "no"
 * 
 * @param db handler db
 * @param result vettore di long risultante
 * @param no numero di utenti online
 */
static inline void exec_getonlineuserfd(sqlite3 *db, long **result, int *no)
{
	init_list_callback(long, par);

	/* numero di utenti attualmente online in modo da allocare il vettore
		con la corretta dimensione */
	exec_getnumberonlineuser(db, &(par.size));
	if (par.size <= 0)
	{
		result = NULL;
		*no = 0;
		return;
	}
	*no = par.size;
	par.result = safe_malloc((par.size) * sizeof(long));
	memset(par.result, 0, (par.size) * sizeof(long));
	*result = par.result;

	exec_query(db, query_getonlineuserfd, getlonglist_callback, &par);
}

//-------------------------------------------------------------------------//

#define query_gettotaluser   \
	"SELECT COUNT(username) " \
	"FROM _User;"

/**
 * @brief fornisce il numero di utenti registrati al server
 * 
 * @param db handler db
 * @param no_user (no_user) | (GETLONG_ERROR)
 */
static inline void exec_gettotaluser(sqlite3 *db, long *no_user)
{
	exec_query(db, query_gettotaluser, getlong_callback, no_user);
}

//-------------------------------------------------------------------------//

#define query_getallusername \
	"SELECT username "        \
	"FROM _User;"

/**
 * @brief restituisce la lista di tutti gli utenti registrati alla chat
 * 
 * @param db handler db
 * @param user_list vettore di stringhe risultante
 * @param no_user numero di utenti registrati
 */
static inline void exec_getallusername(sqlite3 *db, char **user_list, int *no_user)
{
	init_list_callback(string, par);
	exec_gettotaluser(db, (long *)no_user);
	if (*no_user < 1)
	{
		user_list = NULL;
		return;
	}
	par.size = *no_user;
	par.result = safe_malloc((*no_user * (MAX_NAME_LENGTH + 1)) * sizeof(char));
	*user_list = par.result;

	exec_query(db, query_getallusername, getstringlist_callback, &par);
}
//-------------------------------------------------------------------------//

#define query_getnumberonlineusergroup           \
	"SELECT COUNT(_User.username) "               \
	"FROM _User, _Chat_User "                     \
	"WHERE _User.username = _Chat_User.username " \
	"AND _Chat_User.chat_id = '%ld' "             \
	"AND _User.curr_fd >= 0;"

#define fill_getnumberonlineusergroup(p, chat_id) \
	fill_query(p, query_getnumberonlineusergroup, chat_id)

/**
 * @brief restituisce in "result" il numero di utenti attualmente online nel gruppo 
 * 		con id "group_id"
 * 
 * @param db handler db
 * @param group_id id del gruppo
 * @param result (numero di utenti) | (GETLONG_ERROR) se il gruppo non esiste
 */
static inline void exec_getnumberonlineusergroup(sqlite3 *db, long group_id, long *result)
{
	init_param(getnumberonlineusergroup, group_id);
	exec_query(db, q, getlong_callback, result);
	destroy_param;
}
//-------------------------------------------------------------------------//

#define query_delgroup                        \
	"DELETE FROM _Message, _Chat, _Chat_User " \
	"WHERE _Message.chat_id = _Chat.chat_id "  \
	"AND _Chat.chat_id = _Chat_User. chat_id " \
	"AND chat_name = '%s';"

#define query_getgroupowner \
	"SELECT creator "        \
	"FROM _Chat "            \
	"WHERE chat_name = '%s';"

#define fill_delgroup(q, chat_name) \
	fill_query(q, query_delgroup, chat_name)

#define fill_getgroupowner(q, chat_name) \
	fill_query(q, query_getgroupowner, chat_name)

/**
 * @brief esegue l'eliminazione del gruppo "group_name" richiesto da
 * 		"owner"
 * @warning solo il creatore del gruppo può eliminarlo
 * 
 * @param db handler db
 * @param owner utente richiedente
 * @param group_name nome del gruppo
 * @return int (SQLITE_OK) se il gruppo viene eliminato
 * 				(SQLITE_FAIL) altrimenti
 */
static inline int exec_delgroup(sqlite3 *db, char *owner, char *group_name)
{
	init_param(getgroupowner, group_name);
	init_list_callback(string, par);
	par.size = 1;
	par.result = safe_malloc((MAX_NAME_LENGTH + 1) * sizeof(char));

	/* richiedo il proprietario del gruppo */
	exec_query(db, q, getstringlist_callback, &par);
	destroy_param;

	if (par.result == NULL)
		return SQLITE_FAIL;

	if (strcmp(par.result, owner) == 0) /* l'utente è owner del gruppo */
	{
		init_param(delgroup, group_name);
		exec_query(db, q, NULL, NULL);
		destroy_param;
		return SQLITE_OK;
	}

	free(par.result);
	return SQLITE_FAIL;
}
//-------------------------------------------------------------------------//

#define query_removeuser_from_group          \
	"DELETE FROM _Chat_User "                 \
	"WHERE chat_id = '%ld' "                  \
	"AND username = '%s'; "                   \
	"UPDATE _Message "                        \
	"SET sent_by = '#user_no_more_in_group' " \
	"WHERE chat_id = '%ld' "                  \
	"AND sent_by = '%s';"

#define fill_removeuser_from_group(q, chat_id, username) \
	fill_query(q, query_removeuser_from_group, chat_id, username, chat_id, username)

/**
 * @brief rimuove l'utente "username" dal gruppo con id "chat_id"
 * 		e imposta il mittente dei messaggi da lui inviati come
 * 		"#user_no_more_in_group" 
 * @warning REQUIRES: l'utente appartiene al gruppo
 * @warning come per le chat da utenti, i messaggi sono sempre recuperabili
 * 			dai destinatari ancora registrati
 * 
 * @param db 
 * @param chat_id 
 * @param username 
 */
static inline void exec_removeuser_from_group(sqlite3 *db, long chat_id, char *username)
{
	init_param(removeuser_from_group, chat_id, username);
	exec_query(db, q, NULL, NULL);
	destroy_param;
}
//-------------------------------------------------------------------------//

#define query_getonlineuser_in_group             \
	"SELECT _User.curr_fd "                       \
	"FROM _User, _Chat_User "                     \
	"WHERE _User.username = _Chat_User.username " \
	"AND _Chat_User.chat_id = '%ld' "             \
	"AND _User.curr_fd >= 0;"

#define fill_getonlineuser_in_group(p, chat_id) \
	fill_query(p, query_getonlineuser_in_group, chat_id)

/**
 * @brief restituisce in "result" i descrittori associati a tutti gli utenti attualmente
 * 		online nel gruppo con id "group_id"
 * 
 * @param db handler db
 * @param group_id id del gruppo
 * @param result descrittori di tutti gli utenti online
 * @param no numero di utenti online
 * @return int (SQLITE_OK) | (SQLITE_CONSTRAINT)
 */
static inline int exec_getonlineuser_in_group(sqlite3 *db, long group_id, long **result, int *no)
{
	init_list_callback(long, par);

	/* prendo il numero di utenti online in modo da inizializzare
		correttamente la struttura */
	exec_getnumberonlineusergroup(db, group_id, &(par.size));
	if (par.size <= 0)
	{
		result = NULL;
		*no = 0;
		return 0;
	}
	par.result = safe_malloc(par.size * sizeof(long));
	*result = par.result;
	*no = par.size;

	init_param(getonlineuser_in_group, group_id);
	int ret = exec_query(db, q, getlonglist_callback, &par);
	destroy_param;
	return ret;
}

//-------------------------------------------------------------------------//

#define query_getprevmsgs                         \
	"SELECT message, filename, sent_by "           \
	"FROM _Message, _Chat_User "                   \
	"WHERE _Message.chat_id = _Chat_User.chat_id " \
	"AND username = '%s' "                         \
	"AND sent_by <> '%s' "                         \
	"ORDER BY sent_time DESC "                     \
	"LIMIT %d; "

#define fill_getprevmsgs(p, user, max_msgs) \
	fill_query(p, query_getprevmsgs, user, user, max_msgs)

/**
 * @brief effettua il recupero di tutti gli ultimi "max_msgs" (in ordine di 
 * 		tempo decrescente) messaggi destinati ad "user"
 * 		vengono inseriti in "result" i messaggi contenenti tutte le info
 * 		necessarie
 * 
 * @param db handler del db
 * @param user utente che richiede i messaggi
 * @param max_msgs massimo numero di messaggi
 * @param result vettore risultato
 * @return int dimensione del vettore
 */
static inline int exec_getprevmsgs(sqlite3 *db, char *user, int max_msgs, message_t **result)
{
	init_list_callback(message, par);
	par.result = safe_malloc(max_msgs * sizeof(message_t));
#ifdef MAKE_VALGRIND_HAPPY
	memset(par.result, 0, max_msgs * sizeof(message_t));
#endif
	par.size = max_msgs;
	init_param(getprevmsgs, user, max_msgs);

	exec_query(db, q, getmessagelist_callback, &par);
	destroy_param;
	if ((par.curr_pos < par.size) && (par.curr_pos > 0))
	{
		par.result = realloc(par.result, par.curr_pos * sizeof(message_t));
		if (!(par.result))
			handle_error(STRING_BAD_REALLOC);
		par.size = par.curr_pos;
	}
	else if (par.curr_pos == 0)
	{
		par.size = 0;
		free(par.result);
		*result = NULL;
		return par.size;
	}

	for (int i = 0; i < par.size; i++)
		strcpy(par.result[i].data.hdr.receiver, user);

	*result = (par.size <= 0) ? NULL : par.result;
	return par.size;
}

//-------------------------------------------------------------------------//
#ifndef MAKE_TEST_HAPPY

#define query_increasestats                             \
	"UPDATE _Stats "                                     \
	"SET not_delivered_txt = not_delivered_txt + '%d', " \
	"not_delivered_file = not_delivered_file + '%d', "   \
	"delivered_txt = delivered_txt + '%d', "             \
	"delivered_file = delivered_file + '%d', "           \
	"error_numbers = error_numbers + '%d';"

#define fill_increasestats(p, not_txt, not_file, txt, file, err) \
	fill_query(p, query_increasestats, not_txt, not_file, txt, file, err)

/**
 * @brief esegue l'incremento delle statistiche
 * 
 * @param db handler db
 * @param not_txt testuali non inviati
 * @param not_file file non inviati
 * @param txt testuali inviati
 * @param file file inviati
 * @param err messaggi di errore
 */
static inline void exec_increasestats(sqlite3 *db, int not_txt, int not_file, int txt, int file, int err)
{
	init_param(increasestats, not_txt, not_file, txt, file, err);
	exec_query(db, q, NULL, NULL);
	destroy_param;
}

//-------------------------------------------------------------------------//

#define query_get_from_stats \
	"SELECT * "               \
	"FROM _Stats "            \
	"LIMIT 1; "

/**
 * @brief restituisce le statistiche del server
 * 
 * @param db handler db
 * @param not_txt testuali non inviati
 * @param not_file file non inviati
 * @param txt testuali inviati
 * @param file file inviati
 * @param errors messaggi di errore
 */
static inline void exec_get_from_stats(sqlite3 *db, unsigned long *not_txt, unsigned long *not_file, unsigned long *text, unsigned long *file, unsigned long *errors)
{
	unsigned long par[5];
	exec_query(db, query_get_from_stats, getstats_callback, &par);

	*not_txt = par[0];
	*not_file = par[1];
	*text = par[2];
	*file = par[3];
	*errors = par[4];
}

#endif /* make_test_happy */

//-------------------------------------------------------------------------//

#include "message.h"
#include "ops.h"

enum operation
{
	user,
	group
};

/**
 * @brief restituisce la query per la creazione del database

 * @return char* query_createdb
 */
const char *createdb();

/**
 * @brief restituisce la query per la pulizia del database da effettuare
 * 		dopo ogni avvio
 * 
 * @return const char* query_cleardb
 */
const char *cleardb();

//-------------------------------------------------------------------------//

/**
 * @brief astrazione per la creazione di un oggetto risposta
 * 
 * @param ans puntatore al messaggio di risposta
 * @param OP operazione di risposta
 * @param buffer buffer data
 * @param buf_dim dimensione del buffer data
 * @param sender 
 * @param receiver 
 */
void set_reply_message(message_t *ans, op_t OP, char *buffer, int buf_dim, char *sender, char *receiver);

/**
 * @brief restituisce la lista di utenti attualmente online
 * 
 * @param db handler del database
 * @param buf_dim puntatore alla dimensione del buffer restituito (negativo in caso di errore)
 * @return char* lista degli utenti online
 */
char *get_online_user(sqlite3 *db, int *buf_dim);

/**
 * @brief effettua l'inserimento di "user" nel database (se possibile)
 * 		come utente CONNESSO
 * 		e prepara il messaggio di risposta da inviare nel caso di operazione
 * 		andata a buon fine (lista di utenti online)
 * 
 * @param user utente da registrare
 * @param fd descrittore sul quale è connesso
 * @param ans messaggio di risposta
 * @param db handler del db
 * @return op_t l'operazione da inviare come risposta all'utente
 * 				(OP_OK) | (OP_FAIL) | (OP_NICK_UNKNOWN) | ...
 */
op_t manage_insertuser(char *user, int fd, message_t *ans, sqlite3 *db);

/**
 * @brief effettua la connessione dell'utente già registrato "user" (se possibile)
 * 		e prepara il messaggio di risposta da inviare nel caso di operazione
 * 		andata a buon fine (lista di utenti online)
 * 
 * @param user utente da connettere
 * @param fd descrittore sul quale si connette
 * @param ans messaggio di risposta
 * @param db handler del db
 * @return op_t l'operazione da inviare come risposta all'utente
 * 				(OP_OK) | (OP_FAIL) | (OP_NICK_UNKNOWN) | ...
 */
op_t manage_connectuser(char *user, int fd, message_t *ans, sqlite3 *db);

/**
 * @brief effettua la disconnessione (se possibile) dell'utente connesso
 * 		tramite il descrittore "fd"
 * 
 * @param fd descrittore
 * @param db handler db
 */
void manage_disconnectuser(int fd, sqlite3 *db);

/**
 * @brief effettua la creazione del gruppo "group_name" (se possibile)
 * 		effettuata dall'utente "creator"
 * 
 * @param group_name nome del gruppo
 * @param creator utente creatore
 * @param db handler db
 * @return op_t l'operazione da inviare come risposta all'utente
 * 				(OP_OK) | (OP_FAIL) | (OP_NICK_UNKNOWN) | ...
 */
op_t manage_creategroup(char *group_name, char *creator, sqlite3 *db);

/**
 * @brief effettua l'aggiunta dell'utente "user" al gruppo "group_name"
 * 			(se possibile)
 * 
 * @param group_name nome gruppo
 * @param user utente
 * @param db handler db
 * @return op_t l'operazione da inviare come risposta all'utente
 * 				(OP_OK) | (OP_FAIL) | (OP_NICK_UNKNOWN) | ...
 */
op_t manage_addtogroup(char *group_name, char *user, sqlite3 *db);

/**
 * @brief effettua la rimozione dell'utente "user" dal gruppo "group_name"
 * 			(se possibile)
 * 
 * @param group_name nome gruppo
 * @param user utente
 * @param curr_fd fd sul quale è connesso
 * @param db handler db
 * @return op_t l'operazione da inviare come risposta all'utente
 * 				(OP_OK) | (OP_FAIL) | (OP_NICK_UNKNOWN) | ...
 */
op_t manage_removeuserfromgroup(char *group_name, char *user, int curr_fd, sqlite3 *db);

/**
 * @brief effettua la rimozione del gruppo "group_name" effettuata da "user"
 * 		(se possibile)
 * @warning solo il creatore può eliminare il gruppo
 * 
 * @param group_name nome del gruppo
 * @param user richiedente
 * @param db handler db
 * @return op_t l'operazione da inviare come risposta all'utente
 * 				(OP_OK) | (OP_FAIL) | (OP_NICK_UNKNOWN) | ...
 */
op_t manage_deletegroup(char *group_name, char *user, sqlite3 *db);

/**
 * @brief restituisce le statistiche correnti del database
 * 
 * @param db handler del database
 * @param chattyStats vettore di statistiche
 */
void manage_getstats(sqlite3 *db, struct statistics *chattyStats);

/**
 * @brief rimuove un utente dal database (se possibile)
 * 
 * @param user utente
 * @param db handler
 * @return op_t op_t l'operazione da inviare come risposta all'utente
 * 				(OP_OK) | (OP_FAIL) | (OP_NICK_UNKNOWN) | ...
 */
op_t manage_unregisteruser(char *user, sqlite3 *db);

#define NOT_IN_GROUP -3

/**
 * @brief inserisce un messaggio "msg" nel database, restituisce un
 * 		vettore contenente la lista dei file descriptor attivi a cui il 
 * 		server deve recapitare il messaggio 
 * 
 * @param msg il messaggio
 * @param db handler del database
 * @param *no_fd dimensione del vettore restituito
 * @return int* vettore di fd
 */
long *manage_postmessage(message_t *msg, int sender_fd, int *no_fd, enum operation *branch, sqlite3 *db);

/**
 * @brief restituisce (se possibile) un vettore contenente gli ultimi x messaggi 
 * 		(previsti dalla configurazione del server) inviati all'utente sender
 * 
 * @param sender 
 * @param ans 
 * @param db 
 * @return int 
 */
int manage_getprevmsgs(char *sender, message_t **ans, sqlite3 *db);

/**
 * @brief restituisce (se possibile) il messaggio di risposta alla richiesta
 * 		del file inviata tramite "msg"
 * 
 * @param msg messaggio di richiesta
 * @param ans messaggio di risposta (completo)
 * @param db handler db
 * @return op_t op_t l'operazione da inviare come risposta all'utente
 * 				(OP_OK) | (OP_FAIL) | (OP_NICK_UNKNOWN) | ...
 */
op_t manage_getfile(message_t *msg, message_t *ans, sqlite3 *db);

#endif

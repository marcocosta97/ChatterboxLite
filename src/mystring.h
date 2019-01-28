
#ifndef _MY_STRING_H_
#define _MY_STRING_H_

/**
 * @brief contiene la definizione della maggior parte delle stringhe utilizzate
 * 
 * @file mystring.h
 * @author Marco Costa - mc
 * @date 2018-08-28
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */
#include "config.h"

#ifndef MAKE_ISO_COMPILER_HAPPY
#define MAKE_ISO_COMPILER_HAPPY
typedef int make_iso_compilers_happy;
#endif

#define STRINGIZE(x) #x

/**************************************************************************************************
 * 												STRINGHE DI ERRORE
 **************************************************************************************************/

#define STRING_ERROR_QUOTE "[!!]"
#define STRING_VOID_QUOTE "[  ] "

#define STRING_PERROR(str) STRING_ERROR_QUOTE " errore " str ":"
#define STRING_ERROR STRING_ERROR_QUOTE " errore "
#define SEG(str) STRING_ERROR str "\n"
#define SVG(str) STRING_VOID_QUOTE str "\n"

#define STRING_BAD_MALLOC SEG("impossibile allocare la memoria")
#define STRING_BAD_REALLOC SEG("impossibile reallocare la memoria")
#define STRING_BAD_START_ARGUMENTS                               \
	SEG("il server può essere lanciato con il seguente comando:") \
	SVG("\t%s -f conffile")                                       \
	SVG("per utilizzare un file di configurazione predefinito")   \
	SVG("o senza parametri per utilizzare la configurazione predefinita")

#define STRING_BAD_STRTOL STRING_PERROR("strtol")
#define STRING_BAD_FILE_OPEN "apertura file "
#define STRING_BAD_PARSING SEG("nel parsing del file di configurazione, chiusura")
#define STRING_BAD_PARSED_FILE SEG("file di configurazione mal formattato alla linea %d")
#define STRING_BAD_PARSED_NODIGIT SEG("dato non trovato nel file di conf alla linea %d")
#define STRING_BAD_PARSED_OUTOFRANGE SEG("valore out of range nel file di conf alla linea %d")

#define STRING_BAD_DB_OPEN SEG("apertura del database %s")
#define STRING_BAD_QUERY SEG("SQL: %s")
#define STRING_BAD_DB_ACCESS STRING_PERROR("accesso database salvato")

#define STRING_HANDLE_BAD_THREAD_CREATION "creazione thread"
#define STRING_HANDLE_BAD_SOCKET_CREATION "creazione socket"
#define STRING_HANDLE_BAD_BIND "assegnamento indirizzo server"
#define STRING_HANDLE_BAD_LISTEN "ascolto sulla socket"
#define STRING_HANDLE_BAD_SELECT "utilizzo funzione select"
#define STRING_HANDLE_BAD_FILE_WRITING "scrittura su file"
#define STRING_HANDLE_BAD_FILE_READING "lettura su file"
#define STRING_HANDLE_BAD_FOLDER "accesso cartella temporanea"
#define STRING_BAD_ACCEPT "accettazione client"

/**************************************************************************************************
 * 												STRINGHE DI LOG
 **************************************************************************************************/

#define STRING_LOG_QUOTE "[++]"
#define LOG(str) STRING_LOG_QUOTE " " str "\n"

#define STRING_LOG_DBCREATED LOG("database creato")
#define STRING_LOG_DBOPENED LOG("database salvato aperto correttamente")

#define STRING_LOG_DEFCONF LOG("configurazione di default caricata")
#define STRING_LOG_USRCONF LOG("configurazione utente caricata")

#define STRING_LOG_NEWCONN LOG("nuova connessione accettata su fd: %d")

#define STRING_MAX_VALUE_EXCEEDED                                            \
	SEG("file di configurazione, superato il valore limite %d alla linea %d") \
	SVG("utilizzo del valore di default")

/**************************************************************************************************
 * 												STRINGHE DI ERRORE IN RISPOSTA
 **************************************************************************************************/

#define STRING_CLIENT_USER_NICK_ALREADY SEG("utente già registrato precedentemente")
#define STRING_CLIENT_USER_NICK_UNKONWN SEG("l'utente non risulta registrato")
#define STRING_CLIENT_USER_BUSY SEG("l'utente risulta già connesso")
#define STRING_CLIENT_USER_NICK_TOOLONG SEG("l'username scelto supera il limite di " STRINGIZE(MAX_NAME_LENTH) " caratteri")
#define STRING_CLIENT_USER_INVALIDCHAR SEG("l'username inizia con un carattere non permesso")

#endif /* mystring.h */
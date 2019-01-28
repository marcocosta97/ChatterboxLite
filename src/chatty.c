#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

/*
 * membox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */

/**
 * @file chatty.c
 * @brief File principale del server chatterbox, contiene la routine del 
 * 		thread gestore dei segnali, il controllo dei parametri di ingresso,
 * 		l'avvio del parser e l'avvio del server
 * 
 * 
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 05.2018
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/eventfd.h>

#include "sqlite3.h"
#include "config.h"
#include "stats.h"
#include "connections.h"
#include "message.h"
#include "ops.h"
#include "utils.h"
#include "mystring.h"
#include "core.h"
#include "queries.h"

// #include "driver.h"

/* struttura che memorizza le statistiche del server, struct statistics 
 * e' definita in stats.h.
 *
 */
struct statistics chattyStats = {0, 0, 0, 0, 0, 0, 0};

/**
 * @brief file delle statistiche
 * 
 */
static char *conf_stat;

/**
 * @brief wrapper per il passagio dei parametri al thread signal manager
 * 
 */
struct arg_wrapper
{
	int efd;			/**< descrittore di comunicazione tra signal manager e server*/
	sigset_t *set; /**< set dei segnali da monitorare */
};

/**
 * @brief stampa il messaggio di uso
 * 
 * @param progname nome del programma
 */
static void usage(const char *progname)
{
	fprintf(stderr, STRING_BAD_START_ARGUMENTS, progname);
}

/**
 * @brief routine del thread adibito a gestore dei segnali
 * 			- ignora SIGTRAP e SIGPIPE
 * 			- in attesa finché non arrivano SIGUSR1 o segnali di terminazione
 * 
 * @param arg arg_wrapper contenente il descrittore sul quale è attivo il 
 * 								canale di comunicazione verso il server e il
 * 								set di segnali in ascolto
 * @return void* 
 */
void *signal_manager(void *arg)
{
	struct arg_wrapper *w = arg;
	int sig;
	sqlite3 *db;

	open_db(&db);
	for (;;)
	{
		/* mi metto in pausa finché non mi arriva uno dei segnali
			aggiunti al set */
		int ret_value = sigwait(w->set, &sig);
		if (ret_value != 0)
			handle_error("sigwait");

		printf("[!!] ricevuto segnale %d\n", sig);
		if (sig == SIGTRAP || sig == SIGPIPE)
			continue;

		if (sig == SIGUSR1)
		{
			/* Open  for  reading  and appending (writing at end of file).  The
				file is created if it does not exist.  The initial file position
				for  reading  is  at  the  beginning  of the file, but output is
				always appended to the end of the file. */
			FILE *f = fopen(conf_stat, "a+");
			if (f == NULL)
				handle_error(STRING_HANDLE_BAD_FILE_WRITING);
			manage_getstats(db, &chattyStats);
			printStats(f);
#ifdef LOG_MSG
			printStats(stdout);
#endif
			fclose(f);
		}
		/**
		 * @brief segnale di terminazione, avviso il server scrivendo sul 
		 * 		descrittore fasullo un valore > 0 in modo da svegliarlo dalla
		 * 		select
		 */
		else
		{
			int ret_value;

			stop_server();
			uint64_t f = 15; /* un valore a caso > 0 */
			ret_value = write(w->efd, &f, sizeof(uint64_t));
			if (ret_value == -1)
				perror("write");
			printf("[!!] server in terminazione\n");
			free(arg);
			sqlite3_close(db);

			return (void *)0;
		}
	}
}

/**
 * @brief main function, controlla i parametri in ingresso, setta il signal manager, avvia il server (chiama il memory manager?)
 * 
 * @param argc 3
 * @param argv ./progname -f conffile
 * @return int (EXIT_FAILURE) errore, (EXIT_SUCCESS) programma terminato con successo a seguito di un segnale di terminazione SIGINT
 */
int main(int argc, char *argv[])
{
	conf_param *conf_data = NULL;

	if (!((argc == 3) || (argc == 1)))
	{
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	char *command;
	asprintf(&command, "pidof -o %d -x 'chatty' > /dev/null", getpid());

	if (0 == system(command))
	{
		fprintf(stderr, "[!!] il server è già in esecuzione\n");
		free(command);
		exit(EXIT_FAILURE);
	}

	free(command);

#ifdef LOG
	printf("[!!] Server Pid: %d\n", getpid());
#endif

	/**---------------------------------------------------------------------------------------------
	 * @brief 											gestione dei segnali
	 * 
	 * When signal handling needs to be performed on a regular base in a multithreaded environment, 
	 * an interesting approach is to delegate all signals to a separate thread doing nothing else 
	 * but waiting for signals to arrive using sigwait().
	 * 
	 *	To do so:
	 *	
    * 1. Set the signal mask as per the signals you want to handle using pthread_sigmask() 
	 * 	in the "main" thread prior to anything else.
    * 2. Then create the thread to handle the signals.
    * 3. Then block all signals from 1. in the "main" thread by using pthread_sigmask() again.
    * 4. And finally create all other threads.
	 * 
	 * The result would be that all signals specified under 1. would go to the thread created under 2.. 
	 * All other threads would not receive any of the signals specified under 1..
	 * 
	 *----------------------------------------------------------------------------------------------*/

	pthread_t thread_signal_manager;
	sigset_t set, old;

	int efd = eventfd(0, 0); /* molto meno pesante di una pipe */
	if (efd == -1)
	{
		perror("efd");
		exit(EXIT_FAILURE);
	}
	struct arg_wrapper *w = safe_malloc(sizeof(struct arg_wrapper));
	w->efd = efd;

	/* maschero i segnali interessati */
	sigemptyset(&set);
	sigaddset(&set, SIGQUIT);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGINT); /* disattivare se imposto breakpoint */
	sigaddset(&set, SIGUSR1);
#ifdef LOG
	sigaddset(&set, SIGTRAP); /* non voglio chiudermi in caso di debug */
#endif
	sigaddset(&set, SIGPIPE); /* non voglio chiudermi in caso di scrittura su descrittore chiuso */

	pthread_sigmask(SIG_SETMASK, &set, &old);
	w->set = &set;
	pthread_create(&thread_signal_manager, NULL, &signal_manager, (void *)w);
	pthread_detach(thread_signal_manager);

	/* i thread creati a partire da ora ignoreranno i segnali */
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	/**
	 * @brief server lanciato con file di configurazione
	 */
	if (argc == 3)
	{
		FILE *conf_file;

		if (strcmp("-f", argv[1]) != 0)
		{
			usage(argv[0]);
			exit(EXIT_FAILURE);
		}

		conf_file = fopen(argv[2], "r");
		if (!conf_file)
			handle_error(STRING_BAD_FILE_OPEN);

		/* parsing del file di configurazione */
		conf_data = parseconf(conf_file);
		fclose(conf_file);
		if (!conf_data)
		{
			fprintf(stderr, STRING_BAD_PARSING);
			exit(EXIT_FAILURE);
		}

		fprintf(stdout, STRING_LOG_USRCONF);
	}

	else
	{
		/**
		 * @brief avvio senza file di configurazione, imposto la 
		 * 		configurazione di default (in conf.h)
		 */
		conf_data = safe_malloc(sizeof(conf_param));
		init_default_conf(&conf_data);

		fprintf(stdout, STRING_LOG_DEFCONF);
	}

	conf_stat = safe_malloc((strlen(conf_data->stat_filename) + 1) * sizeof(char));
	strcpy(conf_stat, conf_data->stat_filename);

	/**
	 * @brief avvio il server passandogli come parametro la configurazione
	 * 		e il descrittore di comunicazione
	 * 
	 */
	if (!start_core(conf_data, efd))
		stop_core();

	/**
	 * @brief pulizia delle strutture di configurazione
	 * 
	 */
	free_conf_param(conf_data);
	free(conf_stat);

	return EXIT_SUCCESS;
}

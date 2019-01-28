#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE /* See feature_test_macros(7) */
/**
 * @brief semplice batteria di test sul server e la coda
 * 		realizzate prima di poter eseguire i test forniti
 * 
 * @file driver.c
 * @author Marco Costa - mc
 * @date 2018-07-11
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "driver.h"
#include <unistd.h>
#include <signal.h>

#include "connections.h"
#include "message.h"

static volatile sig_atomic_t cycle = 1;

/* test sulla coda */
/*
static void *test_queue_core(void *arg)
{
	int i;

	queue_init();

	for (i = 0; i < 15; i++)
	{
		message_t m;

		m.hdr.sender[0] = (char)('a' + i);
		m.hdr.sender[1] = '\0';

		queue_push(&m);
	}

	return (void *)0;
}

static void *test_queue_slave(void *arg)
{

	while (1)
	{
		message_t m = queue_pop();

		printf("[++] Thread %ld --> %s\n", pthread_self(), m.hdr.sender);
	}

	return (void *)0;
}

void test_queue()
{
	pthread_t core, slave_1, slave_2;

	pthread_create(&core, NULL, &test_queue_core, NULL);
	pthread_setname_np(core, "core");
	pthread_create(&slave_1, NULL, &test_queue_slave, NULL);
	pthread_setname_np(slave_1, "slave_1");
	pthread_create(&slave_2, NULL, &test_queue_slave, NULL);
	pthread_setname_np(slave_2, "slave_2");

	if (core == 0 || slave_1 == 0 || slave_2 == 0)
	{
		fprintf(stderr, "[!!] ERRORE\n");
		exit(EXIT_FAILURE);
	}

	pthread_join(core, NULL);
	pthread_kill(slave_1, SIGUSR1);
	pthread_kill(slave_2, SIGUSR1);

	queue_free();

	printf("[++] Tutto ok\n");
}
*/
/* test client-server */

void test_server()
{
	sleep(1);
	int ret = openConnection("chatty_socket", 10, 1);
	fprintf(stdout, "Client: %d\n", ret);
	if (ret > 0)
	{
		message_hdr_t m;
		setHeader(&m, OP_OK, "ciaone");
		message_t msg;
		msg.hdr = m;
		fprintf(stdout, "invio: %d\n", sendRequest(ret, &msg));
	}
}

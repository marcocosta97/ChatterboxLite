/*
 * membox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */
/**
 * @file config.h
 * @brief File contenente alcune define con valori massimi utilizzabili
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-05-16
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */

#if !defined(CONFIG_H_)
#define CONFIG_H_

#define MAX_NAME_LENGTH 32

#define DEFAULT_UNIX_PATH "/tmp/chatty_socket"
#define DEFAULT_MAX_CONNECTIONS 64
#define DEFAULT_THREADS_IN_POOL 4
#define DEFAULT_MAX_MSG_SIZE 512
#define DEFAULT_MAX_FILE_SIZE 4096 /* 4MB */
#define DEFAULT_MAX_HIST_MSG 32
#define DEFAULT_DIR_NAME "/tmp/chatty"
#define DEFAULT_STAT_FILENAME "/tmp/chatty_stats.txt"

#define MAX_THREADS_IN_POOL 64

// to avoid warnings like "ISO C forbids an empty translation unit"
#ifndef MAKE_ISO_COMPILER_HAPPY
#define MAKE_ISO_COMPILER_HAPPY
typedef int make_iso_compilers_happy;
#endif

#endif /* CONFIG_H_ */

#define _GNU_SOURCE

/**
 * @brief contiene l'implementazione del parser per il file di 
 * 		configurazione e alcune funzioni di utilità generica
 * 
 * @file utils.c
 * @author Marco Costa - 545144 - mcsx97@gmail.com
 * @date 2018-08-29
 * 
 * @note Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
 * 			opera originale dell'autore
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

#include "utils.h"

/**************************************************************************************************
 * 														DEFINE
 **************************************************************************************************/
#define GETLINE_BUFFER_SIZE 64
#define BASE 10

#define LINE curr_line
/**
 * @brief controlli vari su strtol e range dei valori
 * 
 */
#define CHECK_STRTOL(val, endptr, str)                                                        \
	if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0)) \
	{                                                                                          \
		perror(STRING_BAD_STRTOL);                                                              \
		FREE_ROUTINE;                                                                           \
		goto dealloc;                                                                           \
	}                                                                                          \
	if (endptr == str)                                                                         \
	{                                                                                          \
		fprintf(stderr, STRING_BAD_PARSED_NODIGIT, LINE);                                       \
		FREE_ROUTINE;                                                                           \
		goto dealloc;                                                                           \
	}

#define CHECK_RANGE(val)                                   \
	if ((val > INT_MAX) || (val <= 0))                      \
	{                                                       \
		fprintf(stderr, STRING_BAD_PARSED_OUTOFRANGE, LINE); \
		FREE_ROUTINE;                                        \
		goto dealloc;                                        \
	}

/**
 * @brief parsing di un valore long dal file di conf con controllo
 * 
 */
#define sub_parselong(dest, endptr, src) \
	do                                    \
	{                                     \
		long int temp;                     \
		temp = strtol(src, &endptr, BASE); \
		CHECK_RANGE(temp);                 \
		dest = temp;                       \
		CHECK_STRTOL(dest, endptr, src);   \
	} while (0);

#define FREE_ROUTINE      \
	do                     \
	{                      \
		free_conf_param(c); \
		c = NULL;           \
	} while (0);

#define ERR_BAD_PARSED_FILE                               \
	do                                                     \
	{                                                      \
		fprintf(stderr, STRING_BAD_PARSED_FILE, curr_line); \
		FREE_ROUTINE;                                       \
		goto dealloc;                                       \
	} while (0);

/**************************************************************************************************
 * 											DICHIARAZIONI DELLE FUNZIONI
 **************************************************************************************************/
void init_default_conf(conf_param **dest)
{
	conf_param c = {CONF_PARAM_DEFAULT};

	(*dest)->dir_name = safe_malloc((strlen(DEFAULT_DIR_NAME) + 1) * sizeof(char));
	(*dest)->stat_filename = safe_malloc((strlen(DEFAULT_STAT_FILENAME) + 1) * sizeof(char));
	(*dest)->unix_path = safe_malloc((strlen(DEFAULT_UNIX_PATH) + 1) * sizeof(char));
	strcpy((*dest)->dir_name, c.dir_name);
	strcpy((*dest)->stat_filename, c.stat_filename);
	strcpy((*dest)->unix_path, c.unix_path);

	(*dest)->max_connections = c.max_connections;
	(*dest)->max_file_size = c.max_file_size;
	(*dest)->max_hist_msg = c.max_hist_msg;
	(*dest)->threads_in_pool = c.threads_in_pool;
	(*dest)->max_msg_size = c.max_msg_size;
}

void format_string(char *source)
{
	char *i = source;
	char *j = source;
	while (*j != 0)
	{
		*i = *j++;
		if (!isblank(*i) && isascii(*i))
			i++;
	}
	*i = 0;
}

static void sub_parsestring(char **dest, char *src)
{
	int dim = strlen(src) + 1;
	int old_len = strlen(*dest) + 1;
	*dest = safe_c_realloc(*dest, &old_len, dim);
	strcpy(*dest, src);
}

conf_param *parseconf(FILE *f)
{
	char *line = NULL;
	size_t curr_line_dim = GETLINE_BUFFER_SIZE;
	char *endptr = NULL;
	char *data_value = NULL, *data_name = NULL;
	conf_param *c = NULL;

	c = safe_malloc(sizeof(conf_param));
	/* inizializzo la configurazione di default nel caso mancassero dei
		parametri */
	init_default_conf(&c);

	if (f)
	{
		int curr_line = 1;
		line = safe_malloc(GETLINE_BUFFER_SIZE * sizeof(char));
		/**
		 * @brief legge il file di conf per riga, 
		 * NOTA: getLine ridimensiona il buffer in caso la dimensione non bastasse
		 */
		while (getline(&line, &curr_line_dim, f) != EOF)
		{
			/* se la linea non inizia con # o spazio il file è mal formattato */
			if (*line == '#' || isspace(*line))
			{
				curr_line++;
				continue;
			};

			/* rimuovo tutti gli spazi e i caratteri non ASCII */
			format_string(line);
			data_name = strtok(line, "=");
			if (data_name == NULL)
			{
				ERR_BAD_PARSED_FILE;
				break;
			}
			data_value = strtok(NULL, "\n");
			if (data_value == NULL)
			{
				ERR_BAD_PARSED_FILE;
				break;
			}

			/**
			 * @brief switch case sui possibili parametri di configurazione
			 * 
			 */
			if (strcmp(data_name, "UnixPath") == 0)
				sub_parsestring(&(c->unix_path), data_value);
			else if (strcmp(data_name, "DirName") == 0)
				sub_parsestring(&(c->dir_name), data_value);
			else if (strcmp(data_name, "StatFileName") == 0)
				sub_parsestring(&(c->stat_filename), data_value);
			else if (strcmp(data_name, "MaxConnections") == 0)
			{
				sub_parselong(c->max_connections, endptr, data_value);
			}
			else if (strcmp(data_name, "ThreadsInPool") == 0)
			{
				sub_parselong(c->threads_in_pool, endptr, data_value);
				if ((c->threads_in_pool > MAX_THREADS_IN_POOL) || (c->threads_in_pool == 0))
				{
					fprintf(stderr, STRING_MAX_VALUE_EXCEEDED, MAX_THREADS_IN_POOL, LINE);
					c->threads_in_pool = DEFAULT_THREADS_IN_POOL;
				}
			}
			else if (strcmp(data_name, "MaxMsgSize") == 0)
			{
				sub_parselong(c->max_msg_size, endptr, data_value);
			}
			else if (strcmp(data_name, "MaxFileSize") == 0)
			{
				sub_parselong(c->max_file_size, endptr, data_value);
			}
			else if (strcmp(data_name, "MaxHistMsgs") == 0)
			{
				sub_parselong(c->max_hist_msg, endptr, data_value);
			}
			else
			{
				ERR_BAD_PARSED_FILE;
				break;
			}
			curr_line++;
		}
	}
	else
	{
		errno = ENOENT;
		c = NULL;
	}

dealloc:
	if (line)
	{
		free(line);
		line = NULL;
	}

	return c;
}

void free_conf_param(conf_param *c)
{
	if (c->unix_path)
	{
		free(c->unix_path);
		c->unix_path = NULL;
	}
	if (c->dir_name)
	{
		free(c->dir_name);
		c->dir_name = NULL;
	}
	if (c->stat_filename)
	{
		free(c->stat_filename);
		c->stat_filename = NULL;
	}

	if (c)
		free(c);
}

/* brutto come la morte ma è il metodo maggiormente performante in C */
int get_digits_number(int n)
{
	if (n < 0)
		n = (n == INT_MIN) ? INT_MAX : -n;
	if (n < 10)
		return 1;
	if (n < 100)
		return 2;
	if (n < 1000)
		return 3;
	if (n < 10000)
		return 4;
	if (n < 100000)
		return 5;
	if (n < 1000000)
		return 6;
	if (n < 10000000)
		return 7;
	if (n < 100000000)
		return 8;
	if (n < 1000000000)
		return 9;
	/*      2147483647 is 2^31-1 */
	return 10;
}

/**
 * @brief realloca la zona di memoria SENZA CONSERVARE I PRECEDENTI DATI
 * 		- old_len alla fine dell'esecuzione conterrà la nuova dimensione 
 * 
 * @param p puntatore alla vecchia zona di memoria
 * @param old_len puntatore alla vecchia dimensione
 * @param new_len nuova dimensione
 * @return void* zona di memoria allocata
 */
void *safe_c_realloc(void *p, int *old_len, int new_len)
{
	/**
	 * @brief 
    	If you need to keep your data, use realloc(). 
		 	It's ~4 times faster than using malloc()/free() and copying your data
			when scaling up. When scaling down it is 10,000-100,000 times faster. 
			NEVER copy stuff manually.
   -> If you don't need to keep your data, you should use malloc()/free() 
		 	to scale up (increasing memory size) but use realloc() when scaling 
			 down (reducing memory size).
    	If you don't know the previous size (you don't know wether you're scaling 
			down or up), use malloc()/free(). 
			When scaling down, realloc() is ~40 times faster, but when scaling up,
			realloc() is ~7600 times slower. Unless your program does a few huge allocations 
			and tons of little deallocations (~200 times more deallocations than allocations, 
			which can be possible), you should use malloc()/free().
	 * 
	 */
	if (old_len == NULL)
	{
		fprintf(stderr, STRING_BAD_REALLOC);
		exit(EXIT_FAILURE);
	}
	if (*old_len == 0)
	{
		*old_len = new_len;
		return safe_malloc(*old_len * sizeof(char));
	}
	else if (new_len > *old_len)
	{
		free(p);
		*old_len = new_len;
		return safe_malloc(*old_len * sizeof(char));
	}
	else if (new_len < *old_len)
	{
		*old_len = new_len;
		void *temp = realloc(p, new_len * sizeof(char));
		/* impossibile reallocare la memoria */
		if (!temp)
		{
			fprintf(stderr, STRING_BAD_REALLOC);
			exit(EXIT_FAILURE);
		}

		return temp;
	}
	else
		return p;
}
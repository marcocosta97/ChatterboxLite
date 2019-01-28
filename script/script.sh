#! /bin/bash

# script del progetto Chatterbox
#
# autore: Marco Costa - 545144
#
# Si dichiara che l'opera è in ogni sua parte (eccetto ove specificato)
# 	opera originale dell'autore


# messaggio di uso
function usage () {
    echo "uso: $0 [--help] <conf-file> <time>" 1>&2;
}

# controllo il numero di argomenti
if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    usage
    exit 1
    elif [ $# -eq 3 ] &&  ! [ "$1" -eq "--help" ]; then
    usage
    exit 1
    elif [ $# -eq 3 ]; then
    conf=$2
    time=$3
else
    conf=$1
    time=$2
fi

# controllo che $time sia un numero
# https://stackoverflow.com/questions/806906/how-do-i-test-if-a-variable-is-a-number-in-bash
re='^[0-9]+$'
if ! [[ $time =~ $re ]] ; then
    echo "$0: $time non è un numero" 1>&2;
    exit 1
fi

# e che sia > 0
if [[ $time -lt 0 ]]; then
    echo "$0: $time non può essere < 0" 1>&2;
    exit 1
fi

# controllo dei permessi sul file di conf
if ! [ -f $conf ]; then
    echo "$0: il file $conf non esiste" 1>&2;
    exit 1
    elif ! [ -r $conf ]; then
    echo "$0: non hai permessi per leggere il file $conf" 1>&2;
    exit 1
fi

#leggo il file di configurazione finché non trovo la riga DirName
exec 3<$conf
dir="0"
while read -u 3 line ; do
    if [[ $line = "DirName"* ]]; then
        line=${line//[[:blank:]]/}
        dir=${line##DirName=}
        break
    fi
done

#chiudo il descrittore
exec 3<&-

# controllo su esistenza e permessi della directory
if [ "$dir" = "0" ]; then
    echo "$0: DirName non trovato nel file $conf" 1>&2;
    exit 1
fi

if ! [ -d $dir ]; then
    echo "$0: $dir non esiste/non è una directory" 1>&2;
    exit 1
fi

if ! [ -r $dir ] && ! [ -w $dir ]; then
    echo "$0: non hai i permessi di lettura e/o scrittura su %dir" 1>&2;
    exit 1
fi

# se time = 0 stampo il contenuto della directory
if [ $time -eq 0 ]; then
    ls -l $dir
    exit 0
fi

# trovo tutti i file nella directory modificati da almeno $time minuti
# e li utilizzo come input nella creazione dell'archivio

# NOTE: --recursion sui contenuti delle cartelle è di default
# -- null indica che i file sono scanditi dal carattere '\0'
# -czvf 'c' crea archivio, 'z' comprimi, 'v' stampa a video i file compressi
#       'f' nome del file segue
# -P pathname assoluti
# -C spostati su $dir prima di eseguire l'operazione (evita di comprimere
#        tutta la gerarchia)
# --remove-files elimina i file aggiunti all'archivio
# -T prendi lista di nomi come input (o file)
find $dir/* -mmin +$time -print0 | tar -czvf backup.tar.gz -P -C $dir --remove-files --null -T -
#-mmin #+$time#
if [ $? -eq 0 ]; then
    echo "$0: operazione eseguita"
    exit 0
else
    echo "$0: operazione fallita" 1>&2
    exit 1
fi
/*
 ================================================================================================================
 Nom du fichier        : m_file.c
 Projet				   : UE Programmation système avancée - projet : Files de messages
                         M1 : Master Informatique fondamentale et appliquee - Universite Paris Cité .
 Description           : Files de messages pour une communication entre processus tournant sur la même machine

 gcc main.c m_file.c -lrt -pthread -o m_file
 ================================================================================================================
 */

/* la macro_contante _XOPEN_SOURCE uniquement dans linux,
 * inutile dans macOs */
#ifdef __linux__
#define _XOPEN_SOURCE 500
#endif

#include <stdio.h> // perror()
#include <stdlib.h> // malloc(), exit(), atexit(), EXIT_SUCCESS, EXIT_FAILURE
#include <unistd.h> // ftruncate()
#include <fcntl.h> // fcntl : file control ; Objets memoire POSIX : pour les constantes O_
#include <sys/mman.h> // mmap(), munmap() ; Objets memoire POSIX : pour shm_open()
#include <sys/stat.h> // fstat(), Pour les constantes droits d’acces
#include <pthread.h> // pthread_mutexattr_init(), pthread_mutexattr_setpshared(), pthread_mutex_init()
#include <errno.h> // Pour strerror(), la variable errno
#include <string.h> // strerror(), memset(), memmove()
#include <stdarg.h> // Pour acceder a la liste des parametres de l’appel de fonctions avec un nombre variable de parametres
#include <signal.h>
#include <sys/types.h>
#include "m_file.h"

/**
 * Une implémentation en utilisant la mémoire partagée entre les processus ;
 * L’accès parallèle à la file de messages est possible avec une protection appropriée, avec des mutexes/conditions.
 */


// Nous stockons dans une variable globale un pointeur vers notre mutex, pour une utilisation future :
//  une fonction exit_mutex() qu'on appellera automatiquement à chaque exit effectuée au milieu de la section critique
// (à l'aide de la fonction atexit). Objectif de cette fonction : pthread_mutex_unlock( mutex )
pthread_mutex_t* mutex;

// Afin de savoir si au moment du exit le processus était dans la section critique
int flag_processus_dans_section_critique;


void exit_mutex() {

	// Afin de savoir si au moment du exit le processus était dans la section critique
	if (flag_processus_dans_section_critique) {
		int mutex_unlock_result = pthread_mutex_unlock( mutex );
		if(mutex_unlock_result != 0) {
			char* error_msg = strerror( mutex_unlock_result ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_mutex_unlock() : %s \n", error_msg);
			exit (EXIT_FAILURE);
		}
	}

	flag_processus_dans_section_critique = 0;
}

/**
  * Signature   : MESSAGE *m_connexion( const char *nom, int options [, size_t nb_msg, size_t len_max, mode_t mode]);
  * Description : Une fonction qui permet soit de se connecter à une file de message existante, soit de créer
  *               une nouvelle file de messages et s’y connecter.
  *
  * Parametres :
  ** const char *nom : le nom de la file ou NULL pour une file anonyme.
  ** int options     : Options est un « OR » bit-à-bit de constantes suivantes ,
  **                   -- O_RDWR, O_RDONLY, O_WRONLY (exactement une de ces constantes doit être spécifiée).
  **                   -- O_CREAT pour demander la création de la file
  **                   -- O_EXCL, en combinaison avec O_CREAT, indique qu’il faut créer la file seulement si
  **                              elle n’existe pas ; si la file existe déjà, m_connexion doit échouer.
  ** size_t nb_msg   : le nombre (minimal) de messages qu’on peut stocker avant que la file soit pleine
  ** size_t len_max  : la longueur maximale d’un message.
  ** mode_t mode     : les permissions accordées pour la nouvelle file de messages
  **                   (« OR » bit-à-bit des constantes définies pour chmod, cf man 2 chmod).
  *
  * m_connexion est une fonction à nombre variable d’arguments (soit 2, soit 5).
  * Si options ne contient pas O_CREAT, alors la fonction m_connexion n’aura que les deux paramètres nom et options
  *
  * m_connexion retourne un pointeur vers un objet de type MESSAGE qui identifie la file de messages et sera utilisé par d’autres fonctions.
  * En cas d’échec, m_connexion retourne NULL.
  */
MESSAGE *m_connexion(const char *nom, int options, ...) { // Fonction avec un nombre variable de parametres

	// void * malloc (size_t size)
	MESSAGE* file = (MESSAGE*) malloc( sizeof(MESSAGE) );
	if (file == NULL){
		fprintf(stderr, "L'allocation de mémoire à l'aide de la fonction malloc() a échoué \n");
		return NULL; // En cas d’échec, m_connexion retourne NULL
	}

	// On va stocker dans cette variable les valeurs qu'on veut passer plus tard pour l'argument protect de la fonction mmap()
	int mmap_protect;

	// options est un « OR » bit-à-bit de constantes : O_RDWR, O_RDONLY, O_WRONLY, O_CREAT, O_EXCL ....
	// exactement une de ces constantes doit être spécifiée : O_RDWR, O_RDONLY, O_WRONLY
	// Le but ici est de vérifier laquelle des trois constantes a été transférée à la fonction,
	// afin de savoir si on est en mode lecture / écriture / lecture et écriture

    // &  : l'opérateur AND binaire copie un bit dans le résultat s'il existe dans les deux opérandes.
	   if ( ((O_RDWR & options) == O_RDWR)  ) {
		   mmap_protect = PROT_READ | PROT_WRITE;
		   file->type_ouverture_file_de_messages = O_RDWR;

	   } else if ( ((O_WRONLY & options) == O_WRONLY) ){
		   mmap_protect = PROT_READ | PROT_WRITE; // Pour mmap(), pour pouvoir écrire, il faut aussi ajouter la constante de lecture
		   file->type_ouverture_file_de_messages = O_WRONLY;

	   } else if ( ((O_RDONLY & options) == O_RDONLY) ) {
			file->type_ouverture_file_de_messages = O_RDONLY;
		    mmap_protect = PROT_READ ;
	   } else {
		   return NULL; // En cas d’échec, m_connexion retourne NULL
	   }

    // Si options contient O_CREAT, alors la fonction m_connexion aura 3 paramètres de plus :
	size_t nb_msg = 0, len_max = 0;
	mode_t mode = 0;

	// m_connexion est une fonction à nombre variable d’arguments (soit 2, soit 5).
	// Si options ne contient pas O_CREAT, alors la fonction m_connexion n’aura que les deux paramètres nom et options
	if ( (O_CREAT & options) == O_CREAT) { // Si options contient O_CREAT
		// Fonctions avec un nombre variable de parametres,
		// source : https://www.rocq.inria.fr/secret/Anne.Canteaut/COURS_C/cours.pdf

		va_list liste_parametres; // une variable pointant sur la liste des parametres de l’appel

		// va_start(liste_parametres, dernier_parametre);
		// La variable liste_parametres est d’abord initialisee a l’aide de la macro va_start
		// dernier_parametre designe l’identificateur du dernier parametre formel fixe de la fonction.
		va_start(liste_parametres, options);

		// va_arg(liste_parametres, type)
		// On accede aux differents parametres de liste par la macro va_arg
		// qui retourne le parametre suivant de la liste:
		nb_msg = va_arg(liste_parametres, size_t);
		len_max = va_arg(liste_parametres, size_t);
		mode = va_arg(liste_parametres, mode_t);

		// Apres traitement des parametres, on libere la liste a l’aide de va_end :
		va_end(liste_parametres);
	}

	// Taille de l'espace mémoire pour l'objet mémoire POSIX que nous voulons projeter en mémoire à l'aide de mmap()
	int taille_memoire = sizeof(FILE_DE_MESSAGES) + ( nb_msg * sizeof(FILE_ELEMENT) ) + ( len_max * nb_msg );

	void* ptr_mmap = NULL; // le pointeur vers la mémoire partagée qui contient la file
	if (nom != NULL) { // <=> une file PAS anonyme
		// int shm_open(cont char *name, int oflag, mode_t mode);
		// Le troisième paramètre est ignoré si on ouvre un objet mémoire existant.
		// Retourne un descripteur fichier si OK, -1 sinon
		int shm_descripteur = shm_open(nom, options, mode);
		if(shm_descripteur == -1){
			perror("Fonction shm_open()");
			return NULL; // En cas d’échec, m_connexion retourne NULL
		}

		if(nb_msg != 0) { // <=> Si c'est une nouvelle file de messages

			 // int ftruncate (int fd, off_t length)
			 // La fonction ftruncate change la taille d'un fichier a la valeur de length.
			 int ftruncate_result = ftruncate(shm_descripteur, (off_t) taille_memoire);
			 if(ftruncate_result == -1) {
				perror("Fonction ftruncate()"); //  void perror (const char *message)
				return NULL; // En cas d’échec, m_connexion retourne NULL
			 }

		} else { // <=> Si c'est PAS une nouvelle file de messages
			struct stat buf;

			// int fstat (int filedes, struct stat *buf)
			int fstat_resultat = fstat(shm_descripteur, &buf);
			if(fstat_resultat == -1){
				perror("Fonction fstat()");
				return NULL; // En cas d’échec, m_connexion retourne NULL
			}

			taille_memoire = buf.st_size;
		}

		// void * mmap (void *address, size_t length,int protect, int flags, int filedes, off_t offset)
		ptr_mmap = mmap((void *) 0, taille_memoire, mmap_protect , MAP_SHARED, shm_descripteur, 0);
		if(ptr_mmap == MAP_FAILED) {
			perror("Fonction mmap()"); //  void perror (const char *message)
			return NULL; // En cas d’échec, m_connexion retourne NULL
		}

	} else  { // (nom == NULL) => file anonyme

		// void * mmap (void *address, size_t length,int protect, int flags, int filedes, off_t offset)
		ptr_mmap = mmap((void *) 0, taille_memoire, mmap_protect , MAP_ANON | MAP_SHARED, -1, 0);
		if(ptr_mmap == MAP_FAILED) {
			perror("Fonction mmap()"); //  void perror (const char *message)
			return NULL; // En cas d’échec, m_connexion retourne NULL
		}

	}
	// le pointeur vers la mémoire partagée qui contient la file
	file->ptr_memoire_partagee = ptr_mmap;

	FILE_DE_MESSAGES* ptr_file_de_messages = (FILE_DE_MESSAGES *) ptr_mmap;
	if(nb_msg != 0) { // <=> Si c'est une nouvelle file de messages

		ptr_file_de_messages->capacite = nb_msg;  // La longueur maximale d’un message
		ptr_file_de_messages->longueur_maximale_message = len_max;  // capacité de la file (le nombre minimal de messages que la file peut stocker)
		ptr_file_de_messages->nombre_elements_remplis = 0; // le nombre de messages actuellement dans la file
		ptr_file_de_messages->first = 0; // l’indice du premier élément de la file
		ptr_file_de_messages->last = 0; // l’indice de premier élément libre de tableau

		pthread_mutexattr_t attr;

		// int pthread_mutexattr_init(pthread_mutexattr_t *attr);
		int mutexattr_init_result = pthread_mutexattr_init(&attr);
		if(mutexattr_init_result != 0) {
			char* error_msg = strerror( mutexattr_init_result ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_mutexattr_init() : %s \n", error_msg);
			return NULL; // En cas d’échec, m_connexion retourne NULL
		}

		// int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr,int pshared);
		// pshared: PTHREAD_PROCESS_PRIVATE, PTHREAD_PROCESS_SHARED
		// Valeur de retour : 0 si OK, numero d'erreur sinon
		int mutexattr_setpshared_result = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
		if(mutexattr_setpshared_result != 0) {
			char* error_msg = strerror( mutexattr_setpshared_result ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_mutexattr_setpshared() : %s \n", error_msg);
			return NULL; // En cas d’échec, m_connexion retourne NULL
		}

		// int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
		int mutex_init_result = pthread_mutex_init( &(ptr_file_de_messages->mutex), &attr);
		if(mutex_init_result != 0) {
			char* error_msg = strerror( mutex_init_result ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_mutex_init() : %s \n", error_msg);
			return NULL; // En cas d’échec, m_connexion retourne NULL
		}

		pthread_condattr_t condattr;

		// int pthread_condattr_init(pthread_condattr_t *attr);
		// Valeur de retour : 0 si OK, numero d'erreur sinon
		int result_value = pthread_condattr_init(&condattr);
		if(result_value != 0) {
			char* error_msg = strerror( result_value ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_condattr_init() : %s \n", error_msg);
			exit (EXIT_FAILURE);
		}


		// int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared);
		// pshared: PTHREAD_PROCESS_PRIVATE, PTHREAD_PROCESS_SHARED
		// Valeur de retour : 0 si OK, numero d'erreur sinon
		result_value = pthread_condattr_setpshared(&condattr ,PTHREAD_PROCESS_SHARED);
		if(result_value != 0) {
			char* error_msg = strerror( result_value ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_condattr_setpshared() : %s \n", error_msg);
			exit (EXIT_FAILURE);
		}

		// int pthread_cond_init(pthread_cond_t *restrict cond, const pthread_condattr_t *restrict attr);
		// Valeur de retour : 0 si OK, numero d'erreur sinon
		result_value = pthread_cond_init(&(ptr_file_de_messages->attente_file_pleine) ,&condattr);
		if(result_value != 0) {
			char* error_msg = strerror( result_value ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_cond_init() : %s \n", error_msg);
			exit (EXIT_FAILURE);
		}

		result_value = pthread_cond_init(&(ptr_file_de_messages->attente_file_vide) ,&condattr);
		if(result_value != 0) {
			char* error_msg = strerror( result_value ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_cond_init() : %s \n", error_msg);
			exit (EXIT_FAILURE);
		}

		// Pointer vers le debut de la file (debut du tableau circulaire)
		ptr_file_de_messages->tableau_circulaire = (FILE_ELEMENT *) (ptr_file_de_messages + 1);

		int i;
		// Nettoyer (Clear) les elements du tableau circulaire (elements de type FILE_ELEMENT)
		// void * memset (void *block, int c, size_t size)
		for(i = 0 ; i < nb_msg ; i++)
			memset(&ptr_file_de_messages->tableau_circulaire[i], 0, sizeof(FILE_ELEMENT) + ptr_file_de_messages->longueur_maximale_message);

		// Nettoyer (Clear) les elements du tableau circulaire (elements de type FILE_ELEMENT)
		// void * memset (void *block, int c, size_t size)
		for(i = 0 ; i < NB_PROCESSUS ; i++)
			memset(&ptr_file_de_messages->notifications[i], 0, sizeof(ENREGISTREMENT_NOTIFICATIONS));

	} else { // <=> Si c'est PAS une nouvelle file de messages

		// Lorsque nous ouvrons un objet mémoire existant, nous devons mettre à jour le pointeur
		// les adresses virtuelles après chaque mmap sont différentes même si mmap est effectué sur le même objet mémoire.
		ptr_file_de_messages->tableau_circulaire = (FILE_ELEMENT *) (ptr_file_de_messages + 1);
	}

	// Nous stockons dans une variable globale un pointeur vers notre mutex, pour une utilisation future :
	// nous définirons une fonction qu'on appellera automatiquement à chaque exit effectuée au milieu de la section critique
	// (à l'aide de la fonction atexit). Objectif de cette fonction : pthread_mutex_unlock( mutex )
	mutex = &(ptr_file_de_messages->mutex);

	// Afin de savoir si au moment du exit le processus était dans la section critique
	flag_processus_dans_section_critique = 0;
	return file;
}

/**
  * Signature : int m_deconnexion(MESSAGE *file);
  * Description : Une fonction qui déconnecte le processus de la file de messages ;
  *               celle-ci n’est pas détruite, mais devient inutilisable.
  *
  * Parametres :
  ** MESSAGE *file : la file de messages.
  *
  * Valeur de retour : 0 si OK et −1 en cas d’erreur.
  */
int m_deconnexion(MESSAGE *file) {
	FILE_DE_MESSAGES* ptr_file_de_messages = (FILE_DE_MESSAGES *) file->ptr_memoire_partagee;
	int taille_memoire = sizeof(FILE_DE_MESSAGES) + ( ptr_file_de_messages->capacite * sizeof(FILE_ELEMENT) ) + ( ptr_file_de_messages->longueur_maximale_message * ptr_file_de_messages->capacite );

	// int munmap(vois *adr, size_t len)
	return munmap( (void *) ptr_file_de_messages, taille_memoire);
}

/**
  * Signature : int m_destruction(const char *nom);
  * Description : Une fonction qui demande la suppression de la file de messages ;
  *               la suppression effective n’a lieu que quand le dernier processus connecté se déconnecte de la file ;
  *               en revanche, une fois m_destruction() exécutée par un processus, toutes les tentatives de m_connexion ultérieures échouent.
  *
  * Parametres :
  ** const char *nom : le nom de la file.
  *
  * Valeur de retour : 0 si OK, −1 si échec.
  */
int m_destruction(const char *nom) {

	// int shm_unlink(cont char *name);
	return shm_unlink(nom);
}

/**
  * Signature : int m_envoi(MESSAGE *file, const void *msg, size_t len, int msgflag);
  * Description : Une fonction qui envoie le message dans la file.
  *
  * Parametres :
  ** MESSAGE *file   : la file de messages.
  ** const void *msg : un pointeur vers le message à envoyer.
  ** size_t len      : la longueur du message en octets (la longueur du champ mtext de struct mon_message).
  ** int msgflag     : le paramètre msgflag peut prendre deux valeurs,
  **                   -- 0 :  le processus appelant est bloqué jusqu’à ce que le message soit envoyé;
  **                           cela peut arriver quand il n’y a plus de place dans la file
  **                   -- O_NONBLOCK : s’il n’y a pas de place dans la file, alors l’appel retourne tout de suite
  **                                   avec la valeur de retour −1 et errno prend la valeur EAGAIN.
  *
  * Valeur de retour : 0 quand l’envoi réussit, −1 sinon
  * Si la longueur du message est plus grande que la longueur maximale supportée par la file,
  * la fonction retourne immédiatement −1 et met EMSGSIZE dans errno.
  */
int m_envoi(MESSAGE *file, const void *msg, size_t len, int msgflag) {

	// Vérifier que l’opération est autorisée, par exemple m_envoi() échoue si la file a été ouverte seulement en lecture
	if (file->type_ouverture_file_de_messages == O_RDONLY)
		return -1; // échec


	FILE_DE_MESSAGES* ptr_file_de_messages = (FILE_DE_MESSAGES *) file->ptr_memoire_partagee;


	// Si la longueur du message est plus grande que la longueur maximale supportée par la file,
	//la fonction retourne immédiatement −1 et met EMSGSIZE dans errno.
	if(len > ptr_file_de_messages->longueur_maximale_message) {
		errno = EMSGSIZE;
		return -1;  // échec

	}

	int peut_continuer;
	int index_last;

	int mutex_lock_result = pthread_mutex_lock( &(ptr_file_de_messages->mutex) );
	if(mutex_lock_result != 0) {
		char* error_msg = strerror( mutex_lock_result ); // char * strerror (int errnum)
		fprintf(stderr, "Function pthread_mutex_lock() : %s \n", error_msg);
		exit (EXIT_FAILURE);
	}


	/* SECTION CRITIQUE - DEBUT */

	//  une fonction exit_mutex() qu'on appellera automatiquement à chaque exit effectuée au milieu de la section critique
	// (à l'aide de la fonction atexit). Objectif de cette fonction : pthread_mutex_unlock( mutex )
	int atexit_result = atexit(exit_mutex); // int atexit (void (*function) (void))

	if(atexit_result != 0) {
		fprintf(stderr,"Fonction atexit() a echoue \n");

		int mutex_unlock_result = pthread_mutex_unlock( &(ptr_file_de_messages->mutex) );
		if(mutex_unlock_result != 0) {
			char* error_msg = strerror( mutex_unlock_result ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_mutex_unlock() : %s \n", error_msg);
		}

		exit(EXIT_FAILURE);
	}

	// Afin de savoir si au moment du exit le processus était dans la section critique
	flag_processus_dans_section_critique = 1;


	if(ptr_file_de_messages->capacite == ptr_file_de_messages->nombre_elements_remplis) { // Si pas de place dans la file

		switch(msgflag) {
			case O_NONBLOCK : // Si pas de place dans la file, alors lappel retourne tout de suite avec la valeur de retour −1
				              // et errno prend la valeur EAGAIN.
				              errno = EAGAIN; // errno prend la valeur EAGAIN
			                  return -1; // échec
				              //break; Pas besioin de break car return
			case 0 : // Le processus appelant est bloqué jusqu’à ce que le message soit envoyé
				      peut_continuer = 0;
				      break;
			default : return -1; // échec
		}

	} else {
		peut_continuer = 1;
	}



	// Attendre la condition
	while( ! peut_continuer ){
	   // int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex);
	  // Valeur de retour : 0 si OK, numero d'erreur sinon
		int result_value = pthread_cond_wait( &(ptr_file_de_messages->attente_file_pleine), &(ptr_file_de_messages->mutex) );
		if(result_value != 0) {
			char* error_msg = strerror( result_value ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_cond_wait() : %s \n", error_msg);
			exit (EXIT_FAILURE);
		}

		if(ptr_file_de_messages->nombre_elements_remplis < ptr_file_de_messages->capacite)
			peut_continuer = 1;
	}


	index_last = ptr_file_de_messages->last;
	ptr_file_de_messages->last++;

	if(ptr_file_de_messages->last == ptr_file_de_messages->capacite)
		ptr_file_de_messages->last = 0;

	ptr_file_de_messages->nombre_elements_remplis++;


	/* SECTION CRITIQUE - FIN */

	int mutex_unlock_result = pthread_mutex_unlock( &(ptr_file_de_messages->mutex) );
	if(mutex_unlock_result != 0) {
		char* error_msg = strerror( mutex_unlock_result ); // char * strerror (int errnum)
		fprintf(stderr, "Fonction pthread_mutex_unlock() : %s \n", error_msg);
		exit (EXIT_FAILURE);
	}



	// Afin de savoir si au moment du exit le processus était dans la section critique
	flag_processus_dans_section_critique = 0;

	struct mon_message* ptr_message = (struct mon_message *) msg;
	ptr_file_de_messages->tableau_circulaire[index_last + (index_last * ptr_file_de_messages->longueur_maximale_message)].longueur_message = len;
	ptr_file_de_messages->tableau_circulaire[index_last + (index_last * ptr_file_de_messages->longueur_maximale_message)].type = ptr_message->type;
	ptr_file_de_messages->tableau_circulaire[index_last + (index_last * ptr_file_de_messages->longueur_maximale_message)].message = &(ptr_file_de_messages->tableau_circulaire[index_last + (index_last * ptr_file_de_messages->longueur_maximale_message)]) + 1;

	//void * memmove (void *to, const void *from, size_t size)
	memmove(ptr_file_de_messages->tableau_circulaire[index_last + (index_last * ptr_file_de_messages->longueur_maximale_message)].message, ptr_message->mtext, len);


	// Signaler le nouveau message à tous les processus suspendu sur la condition

	// int pthread_cond_signal(pthread_cond_t *cond);
	// Valeur de retour : 0 si OK, numero d'erreur sinon
	int result_value =  pthread_cond_signal( &(ptr_file_de_messages->attente_file_vide) );
	if(result_value != 0) {
		char* error_msg = strerror( result_value ); // char * strerror (int errnum)
		fprintf(stderr, "Fonction pthread_cond_signal() : %s \n", error_msg);
		exit (EXIT_FAILURE);
	}

	int i;
	for(i = 0 ; i < NB_PROCESSUS ; i++) {
		if (ptr_file_de_messages->notifications[i].type == ptr_message->type ) {
			// int kill (pid_t pid, int signum)
			int kill_result = kill(ptr_file_de_messages->notifications[i].pid, ptr_file_de_messages->notifications[i].signum);
			if (kill_result == -1){
				perror("kill()");
				exit(EXIT_FAILURE);
			}

			// Quand le signal de notification est envoyé, le processus enregistré doit être automatiquement désenregistré.
			memset(&ptr_file_de_messages->notifications[i], 0, sizeof(ENREGISTREMENT_NOTIFICATIONS));
		}
	}

	return 0 ; // La fonction retourne 0 quand l’envoi réussit
}

/**
  * Signature : ssize_t m_reception(MESSAGE *file, void *msg, size_t len, long type, int flags);
  * Description : Une fonction qui lit le premier message convenable sur la file, le copie à l’adresse msg et le
  *               supprime de la file. Une file de messages est une file FIFO : si on écrit les messages a, b, c dans cet ordre,
  *               et si on lit avec type==0, alors les lectures successives retournent a, b, c dans le même ordre.
  *
  * Parametres :
  ** MESSAGE *file : la file de messages.
  ** void *msg     : l’adresse à laquelle la fonction doit copier le message lu ; ce message sera supprimé de la file.
  ** size_t len    : la longueur (en octets) de mémoire à l’adresse msg.
  ** long type     : Le paramètre type précise la demande,
  **                 -- type == 0 : lire le premier message dans la file
  **                 -- type > 0 : lire le premier message dont le type est type ;
  **                    grâce au type, plusieurs processus peuvent utiliser la même file de messages,
  **                    par exemple chaque processus peut utiliser son pid comme valeur de type ;
  **                 -- type < 0 : traiter la file comme une file de priorité ; en l’occurrence, cela signifie :
  **                               lire le premier message dont le type est inférieur ou égal à la valeur absolue de type.
  ** int flags     : Le paramètre flags peut prendre soit la valeur 0, soit O_NONBLOCK
  **                 -- flags == 0 : l’appel est bloquant jusqu’à ce que la lecture réussisse.
  **                 -- si flags == O_NONBLOCK, et s’il n’y a pas de message du type demandé dans la file,
  **                                l’appel retourne tout de suite avec la valeur −1 et errno == EAGAIN.
  *
  * Valeur de retour : le nombre d’octets du message lu, ou -1 en cas d’échec.
  * si len est inférieur à la longueur du message à lire, m_reception() échoue et retourne −1 et errno prend la valeur EMSGSIZE.
  */
ssize_t m_reception(MESSAGE *file, void *msg, size_t len, long type, int flags){

	// Vérifier que l’opération est autorisée, par exemple m_reception() échoue si la file a été ouverte seulement en ecriture
	if (file->type_ouverture_file_de_messages == O_WRONLY)
		return -1; // échec

	FILE_DE_MESSAGES* ptr_file_de_messages = (FILE_DE_MESSAGES *) file->ptr_memoire_partagee;

	int peut_continuer;
	int index_first;

	int mutex_lock_result = pthread_mutex_lock( &(ptr_file_de_messages->mutex) );
	if(mutex_lock_result != 0) {
		char* error_msg = strerror( mutex_lock_result ); // char * strerror (int errnum)
		fprintf(stderr, "Function pthread_mutex_lock() : %s \n", error_msg);
		exit (EXIT_FAILURE);
	}

	/* SECTION CRITIQUE - DEBUT */

	//  une fonction exit_mutex() qu'on appellera automatiquement à chaque exit effectuée au milieu de la section critique
	// (à l'aide de la fonction atexit). Objectif de cette fonction : pthread_mutex_unlock( mutex )
	int atexit_result = atexit(exit_mutex); // int atexit (void (*function) (void))

	if(atexit_result != 0) {
		fprintf(stderr,"Fonction atexit() a echoue \n");

		int mutex_unlock_result = pthread_mutex_unlock( &(ptr_file_de_messages->mutex) );
		if(mutex_unlock_result != 0) {
			char* error_msg = strerror( mutex_unlock_result ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_mutex_unlock() : %s \n", error_msg);
		}

		exit(EXIT_FAILURE);
	}

	// Afin de savoir si au moment du exit le processus était dans la section critique
	flag_processus_dans_section_critique = 1;

	if(ptr_file_de_messages->nombre_elements_remplis == 0) { // Si la file est vide

		switch(flags) {
			case O_NONBLOCK : // Si il ny a pas de message du type demandé dans la file,
				             // l’appel retourne tout de suite avec la valeur −1 et errno == EAGAIN.
				              errno = EAGAIN; // errno prend la valeur EAGAIN
			                  return -1; // échec
				              //break; Pas besioin de break car return
			case 0 : // L’appel est bloquant jusqu’à ce que la lecture réussisse.
				      peut_continuer = 0;
				      break;
			default : return -1; // échec
		}

	} else {
		peut_continuer = 1;
	}

	// Attendre la condition
	while( ! peut_continuer ){

	   // int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex);
	  // Valeur de retour : 0 si OK, numero d'erreur sinon
		int result_value = pthread_cond_wait( &(ptr_file_de_messages->attente_file_vide), &(ptr_file_de_messages->mutex) );
		if(result_value != 0) {
			char* error_msg = strerror( result_value ); // char * strerror (int errnum)
			fprintf(stderr, "Fonction pthread_cond_wait() : %s \n", error_msg);
			exit (EXIT_FAILURE);
		}

		if(ptr_file_de_messages->nombre_elements_remplis > 0)
			peut_continuer = 1;


	}

	index_first = ptr_file_de_messages->first;
	ptr_file_de_messages->first++;
	if(ptr_file_de_messages->first == ptr_file_de_messages->capacite)
		ptr_file_de_messages->first = 0;

	ptr_file_de_messages->nombre_elements_remplis--;

	/* SECTION CRITIQUE - FIN */

	int mutex_unlock_result = pthread_mutex_unlock( &(ptr_file_de_messages->mutex) );
	if(mutex_unlock_result != 0) {
		char* error_msg = strerror( mutex_unlock_result ); // char * strerror (int errnum)
		fprintf(stderr, "Fonction pthread_mutex_unlock() : %s \n", error_msg);
		exit (EXIT_FAILURE);
	}

	// Afin de savoir si au moment du exit le processus était dans la section critique
	flag_processus_dans_section_critique = 0;

	ssize_t nombre_octets_message_lu = ptr_file_de_messages->tableau_circulaire[index_first + (index_first * ptr_file_de_messages->longueur_maximale_message)].longueur_message;
	if (len < nombre_octets_message_lu) {
		errno = EMSGSIZE;
		return -1; // échec
	}

	// void * memmove (void *to, const void *from, size_t size)
	memmove(msg, ptr_file_de_messages->tableau_circulaire[index_first + (index_first * ptr_file_de_messages->longueur_maximale_message)].message, nombre_octets_message_lu);

	// void * memset (void *block, int c, size_t size)
	memset(&ptr_file_de_messages->tableau_circulaire[index_first + (index_first * ptr_file_de_messages->longueur_maximale_message)], 0,  sizeof(FILE_ELEMENT) + ptr_file_de_messages->longueur_maximale_message);

	// Signaler la nouvelle place libre à tous les processus suspendu sur la condition

	// int pthread_cond_signal(pthread_cond_t *cond);
	// Valeur de retour : 0 si OK, numero d'erreur sinon
	int result_value =  pthread_cond_signal( &(ptr_file_de_messages->attente_file_pleine) );
	if(result_value != 0) {
		char* error_msg = strerror( result_value ); // char * strerror (int errnum)
		fprintf(stderr, "Fonction pthread_cond_signal() : %s \n", error_msg);
		exit (EXIT_FAILURE);
	}

	return nombre_octets_message_lu;
}

/* L’état de la file */

/**
  * Signature   : size_t m_message_len(MESSAGE* message);
  * Description : Une fonction qui retourne la taille maximale d’un message.
  *
  * Parametres :
  ** MESSAGE* file : la file de messages.
  */
size_t m_message_len(MESSAGE* file){
	FILE_DE_MESSAGES* ptr_file_de_messages = (FILE_DE_MESSAGES *) file->ptr_memoire_partagee;

	return ptr_file_de_messages->longueur_maximale_message;
}

/**
  * Signature   : size_t m_capacite(MESSAGE* file);
  * Description : Une fonction qui retourne le nombre maximal de messages dans la file.
  *
  * Parametres :
  ** MESSAGE* file : la file de messages.
  */
size_t m_capacite(MESSAGE* file){
	FILE_DE_MESSAGES* ptr_file_de_messages = (FILE_DE_MESSAGES *) file->ptr_memoire_partagee;

	return ptr_file_de_messages->capacite;
}

/**
  * Signature   : size_t m_nb(MESSAGE* file);
  * Description : Une fonction qui retourne le nombre de messages actuellement dans la file.
  *
  * Parametres :
  ** MESSAGE* file : la file de messages.
  */
size_t m_nb(MESSAGE* file){
	FILE_DE_MESSAGES* ptr_file_de_messages = (FILE_DE_MESSAGES *) file->ptr_memoire_partagee;

	return ptr_file_de_messages->nombre_elements_remplis;
}

/**
  * Signature   : int enregistrement_notifications(MESSAGE* file, long type, int signal, pid_t pid);
  * Description : Un processus peut s’enregistrer sur la file de messages pour recevoir un signal quand
  *               un message arrive dans la file.
  *
  * Parametres :
  ** MESSAGE *file : la file de messages.
  ** void *msg     : l’adresse à laquelle la fonction doit copier le message lu ; ce message sera supprimé de la file.
  ** size_t len    : la longueur (en octets) de mémoire à l’adresse msg.
  ** long type     : Le paramètre type précise la demande,
  **                 -- type == 0 : lire le premier message dans la file
  **                 -- type > 0 : lire le premier message dont le type est type ;
  **                    grâce au type, plusieurs processus peuvent utiliser la même file de messages,
  **                    par exemple chaque processus peut utiliser son pid comme valeur de type ;
  **                 -- type < 0 : traiter la file comme une file de priorité ; en l’occurrence, cela signifie :
  **                               lire le premier message dont le type est inférieur ou égal à la valeur absolue de type.
  ** int flags     : Le paramètre flags peut prendre soit la valeur 0, soit O_NONBLOCK
  **                 -- flags == 0 : l’appel est bloquant jusqu’à ce que la lecture réussisse.
  **                 -- si flags == O_NONBLOCK, et s’il n’y a pas de message du type demandé dans la file,
  **                                l’appel retourne tout de suite avec la valeur −1 et errno == EAGAIN.
  *
  * Valeur de retour : le nombre d’octets du message lu, ou -1 en cas d’échec.
  */
int enregistrement_notifications(MESSAGE* file, long type, int signum) {

	FILE_DE_MESSAGES* ptr_file_de_messages = (FILE_DE_MESSAGES *) file->ptr_memoire_partagee;
	pid_t pid = getpid(); // pid_t getpid (void)
	int i;

	// Contrôle si signum ∈ [1..NSIG]
	if (signum < 1 || signum > NSIG)
		return -1;

	for(i = 0 ; i < NB_PROCESSUS ; i++) {
		if(ptr_file_de_messages->notifications[i].pid == 0) { // Il y a de la place disponible pour une nouvelle enregistrement

			// int kill (pid_t pid, int signum)
			// si signum == 0 aucun signal n'est envoyé; kill vérifie si le signal peut être envoyé.
			int kill_result = kill(pid, 0);
			if (kill_result == -1)
				return -1;

			ptr_file_de_messages->notifications[i].pid = pid;
			ptr_file_de_messages->notifications[i].signum = signum;
			ptr_file_de_messages->notifications[i].type = type;

			return 1;
		}
	}

	return -1;
}

int annuler_enregistrement(MESSAGE* file) {

	FILE_DE_MESSAGES* ptr_file_de_messages = (FILE_DE_MESSAGES *) file->ptr_memoire_partagee;
	pid_t pid = getpid(); // pid_t getpid (void)
	int i;

	for(i = 0 ; i < NB_PROCESSUS ; i++) {
		if(ptr_file_de_messages->notifications[i].pid == pid) {
			// void * memset (void *block, int c, size_t size)
			memset(&ptr_file_de_messages->notifications[i], 0, sizeof(ENREGISTREMENT_NOTIFICATIONS));
			return 1;
		}
	}

	return -1;
}

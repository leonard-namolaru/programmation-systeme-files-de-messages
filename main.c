/*
 ================================================================================================================
 Nom du fichier        : main.c
 Projet				   : UE Programmation système avancée - projet : Files de messages
                         M1 : Master Informatique fondamentale et appliquee - Universite Paris Cité .
 Description           : Files de messages pour une communication entre processus tournant sur la même machine

 gcc main.c m_file.c -lrt -pthread -o m_file
 ================================================================================================================
 */

#include <stdlib.h> // exit() EXIT_SUCCESS, EXIT_FAILURE
#include <stdio.h> // printf()
#include <fcntl.h> // fcntl : file control ; m_connexion() : pour les constantres O_
#include <sys/stat.h> // pour les constantes droits d’acces
#include <errno.h>
#include <unistd.h> // getpid(), write()
#include <string.h> // memmove()
#include "m_file.h" // m_connexion()
#include <sys/wait.h>
#include <sys/mman.h>

#define N_PROCESSUS 8

void construction_et_envoi_message(MESSAGE* file, long type, void* message, size_t len, int msgflag) {

	// void * malloc (size_t size)
	struct mon_message *m = malloc( sizeof( struct mon_message ) + len );
	if( m == NULL ){
	   // Traiter erreur de malloc()
		fprintf(stderr, "L'allocation de memoire a l'aide de la fonction malloc() a echoue \n");
		exit(EXIT_FAILURE);
	}

	m -> mtext = m + 1;
	m->type = type;

	// copier le message à envoyer
	memmove( m -> mtext, message, len) ;

	m_envoi( file, m, len, msgflag) ;

	free( m );

}

void lecture_message(MESSAGE* file, long type, size_t len, int flags){

	void* buffer = malloc( m_message_len(file) );

	ssize_t reception_resultat = m_reception(file, buffer, len, type, flags);

	if (reception_resultat != -1)
		write(1, buffer, reception_resultat);

	// void free (void *ptr)
	free(buffer);
}

/**
 * Jeu de tests permettant de vérifier que nos fonctions sont capables
 * d’accomplir les tâches demandées, en particulier quand plusieurs processus
 * lancés en parallèle envoient et réceptionnent les messages.
 */
int main(int argc, char* argv[]) {

	size_t nb_msg = 5;
	size_t len_max = 25;

	MESSAGE * file = m_connexion("/projet_file_msg", O_RDWR | O_CREAT, nb_msg, len_max, 0666);

	if(file == NULL) // En cas d’échec, m_connexion retourne NULL
		exit(EXIT_FAILURE);

	int i; // index de la boucle for
	pid_t fork_resulat;
	for(i = 0 ; i < N_PROCESSUS ; i++) {
		fork_resulat = fork();

		if(fork_resulat == -1) {
			perror("Fonction fork()");
			exit(EXIT_FAILURE);
		}

		if(fork_resulat == 0){ // <=> Processus fils
			if (i < (N_PROCESSUS /2)) {
				i = 0;
				while (i < 8) {
					lecture_message(file, 0,  m_message_len(file), 0);
					i++;
				}

			} else {

				i = 0;
				while (i < 8) {
					char t[26];

					// valeur à envoyer
					// int snprintf (char *s, size_t size, const char *template, ...)
					snprintf(t, (len_max + 1), "Bonjour c'est %d ! \n", (int) getpid());

					// comme type de message, on choisit l’identité de l’expéditeur
					construction_et_envoi_message(file, (long) getpid(), t, strlen(t) + 1, 0);

					i++;
				}
			} // else

			char t[26];

			// valeur à envoyer
			// int snprintf (char *s, size_t size, const char *template, ...)
			snprintf(t, (len_max + 1), "Dernier msg %d ! \n", (int) getpid());

			// comme type de message, on choisit l’identité de l’expéditeur
			construction_et_envoi_message(file, (long) getpid(), t, strlen(t) + 1, 0);

			return 0;
		} // if
	} // for


    /* Processus pere */

	i = 0;
	while (i < N_PROCESSUS - 1) {
		lecture_message(file, 0,  m_message_len(file), 0);
		i++;
	}


	// attente de terminaison de tous les enfants
	int status;
	pid_t pid_enfant;

	// Si le processus appelant n'a pas d'enfants wait() retourne -1  et errno == ECHILD
	while( ( ( pid_enfant = wait(&status) ) != -1) && (errno != ECHILD) );

	int taille_memoire = sizeof(FILE_DE_MESSAGES) + ( nb_msg * sizeof(FILE_ELEMENT) ) + ( len_max * nb_msg );

	// int msync (void *address, size_t length, int flags)
	int msync_result = msync(file->ptr_memoire_partagee, taille_memoire, MS_SYNC);
	if(msync_result < 0) {
		perror("Fonction msync()");
		exit(EXIT_FAILURE);
	}

	return EXIT_SUCCESS;
}

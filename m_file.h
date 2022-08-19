/*
 ================================================================================================================
 Nom du fichier        : m_file.h
 Projet				   : UE Programmation système avancée - projet : Files de messages
                         M1 : Master Informatique fondamentale et appliquee - Universite Paris Cité .
 Description           : Files de messages pour une communication entre processus tournant sur la même machine
 ================================================================================================================
 */

#ifndef M_FILE_H_
	#define M_FILE_H_
	#include <pthread.h> // pthread_mutex_t

	#define NB_PROCESSUS 10 // Le nombre de processus qui peuvent être enregistrés en même temps est limité.

	struct mon_message{
		long type; // le type du message
		void* mtext; // le message lui-même
	};

	// le type MESSAGE identifie la file de messages et sera utilisé par de nombreuses fonctions
	typedef struct ptr_file {
		int   type_ouverture_file_de_messages; // lecture, écriture, lecture et écriture
		void* ptr_memoire_partagee; // le pointeur vers la mémoire partagée qui contient la file
	} MESSAGE ;

	typedef struct message_file {
		long  type; // le type du message
		void* message; // le message lui-même
		int   longueur_message; //  le nombre d’octets dans le message (nécessaire pour la valeur de retour de m_reception)
	} FILE_ELEMENT ;

	typedef struct enregistrement_notifications {
		long  type; // le type du message
		int signum; // Quand le processus s’enregistre, il doit indiquer quel signal il veut recevoir
		pid_t   pid;
	} ENREGISTREMENT_NOTIFICATIONS ;

	/**
	 * Une structure qui contient des informations générales sur l’état de la file de messages
	 * et un pointer vers le debut de la file (debut du tableau circulaire)
	 */
	typedef struct file_de_messages {
		ENREGISTREMENT_NOTIFICATIONS notifications[NB_PROCESSUS];

		size_t longueur_maximale_message; // La longueur maximale d’un message
		size_t capacite; // capacité de la file (le nombre minimal de messages que la file peut stocker)
		size_t nombre_elements_remplis; // le nombre de messages actuellement dans la file

		int first; // l’indice du premier élément de la file, celui qui sera lu par m_reception.
		int last; // l’indice de premier élément libre de tableau, celui que m_envoi utilisera pour placer le nouveau message.

		pthread_cond_t attente_file_pleine; // Si la file d'attente est pleine et que le processus souhaite attendre qu'une place se libère
		pthread_cond_t attente_file_vide; // Si la file d'attente est vide et que le processus souhaite attendre
		pthread_mutex_t mutex;

		FILE_ELEMENT* tableau_circulaire; // Pointer vers le debut de la file (debut du tableau circulaire)
	} FILE_DE_MESSAGES ;


	/**
	 * Signature   : MESSAGE *m_connexion( const char *nom, int options [, size_t nb_msg, size_t len_max, mode_t mode]);
	 * Description : Une fonction qui permet soit de se connecter à une file de message existante, soit de créer
	 *               une nouvelle file de messages et s’y connecter.
	 *
	 * m_connexion retourne un pointeur vers un objet de type MESSAGE qui identifie la file de messages et sera utilisé par d’autres fonctions.
	 * En cas d’échec, m_connexion retourne NULL.
	 */
	MESSAGE *m_connexion(const char *nom, int options, ...);  // Fonction avec un nombre variable de parametres

	/**
	  * Signature : int m_deconnexion(MESSAGE *file);
	  * Description : Une fonction qui déconnecte le processus de la file de messages ;
	  *               celle-ci n’est pas détruite, mais devient inutilisable.
	  *
	  * Valeur de retour : 0 si OK et −1 en cas d’erreur.
	  */
	int m_deconnexion(MESSAGE *file);

	/**
	  * Signature : int m_destruction(const char *nom);
	  * Description : Une fonction qui demande la suppression de la file de messages ;
	  *               la suppression effective n’a lieu que quand le dernier processus connecté se déconnecte de la file ;
	  *               en revanche, une fois m_destruction() exécutée par un processus, toutes les tentatives de m_connexion ultérieures échouent.
	  *
	  * Valeur de retour : 0 si OK, −1 si échec.
	  */
	int m_destruction(const char *nom);

	/**
	  * Signature : int m_envoi(MESSAGE *file, const void *msg, size_t len, int msgflag);
	  * Description : Une fonction qui envoie le message dans la file.
	  *
	  * Valeur de retour : 0 quand l’envoi réussit, −1 sinon
	  * Si la longueur du message est plus grande que la longueur maximale supportée par la file,
	  * la fonction retourne immédiatement −1 et met EMSGSIZE dans errno.
	  */
	int m_envoi(MESSAGE *file, const void *msg, size_t len, int msgflag);

	/**
	  * Signature : ssize_t m_reception(MESSAGE *file, void *msg, size_t len, long type, int flags);
	  * Description : Une fonction qui lit le premier message convenable sur la file, le copie à l’adresse msg et le
	  *               supprime de la file. Une file de messages est une file FIFO : si on écrit les messages a, b, c dans cet ordre,
	  *               et si on lit avec type == 0, alors les lectures successives retournent a, b, c dans le même ordre.
	  *
	  * Valeur de retour : le nombre d’octets du message lu, ou -1 en cas d’échec.
	  * si len est inférieur à la longueur du message à lire, m_reception() échoue et retourne −1 et errno prend la valeur EMSGSIZE.
	  */
	ssize_t m_reception(MESSAGE *file, void *msg, size_t len, long type, int flags);

	/* L’état de la file */

	/**
	  * Signature   : size_t m_message_len(MESSAGE* message);
	  * Description : Une fonction qui retourne la taille maximale d’un message.
	  */
	size_t m_message_len(MESSAGE *);

	/**
	  * Signature   : size_t m_capacite(MESSAGE* file);
	  * Description : Une fonction qui retourne le nombre maximal de messages dans la file.
	  */
	size_t m_capacite(MESSAGE *);

	/**
	  * Signature   : size_t m_nb(MESSAGE* file);
	  * Description : Une fonction qui retourne le nombre de messages actuellement dans la file.
	  */
	size_t m_nb(MESSAGE *);

#endif /* M_FILE_H_ */

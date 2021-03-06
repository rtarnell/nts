/* RT/NTS -- a lightweight, high performance news transit server. */
/* 
 * Copyright (c) 2011-2013 River Tarnell.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely. This software is provided 'as-is', without any express or implied
 * warranty.
 */

#ifndef	NTS_SERVER_H
#define	NTS_SERVER_H

#include	<sys/types.h>
#include	<sys/socket.h>
#include	<netinet/in.h>

#include	"filter.h"
#include	"queue.h"
#include	"database.h"
#include	"spool.h"
#include	"client.h"

struct feeder;
struct article;
struct client;

typedef struct hostlist_entry {
	char				*hl_host;
	SLIST_ENTRY(hostlist_entry)	 hl_list;
} hostlist_entry_t;

typedef SLIST_HEAD(hostlist, hostlist_entry) hostlist_t;

typedef struct server_backlog_entry {
	spool_pos_t				 sbe_pos;
	SIMPLEQ_ENTRY(server_backlog_entry)	 sbe_list;
} server_backlog_entry_t;

typedef SIMPLEQ_HEAD(server_backlog_list, server_backlog_entry)
		server_backlog_list_t;

#define SERVER_MAXCONNS_DEFAULT	15

typedef enum {
	QT_Q,
	QT_DEFERRED
} q_type_t;

typedef struct qent {
	q_type_t		 qe_type;
	enum {
		QE_CHECK,
		QE_TAKETHIS
	}			 qe_cmd;
	char			*qe_msgid;
	spool_pos_t		 qe_pos;
	TAILQ_ENTRY(qent)	 qe_list;
} qent_t;

typedef TAILQ_HEAD(sendq, qent) sendq_t;

qent_t	*qealloc(void);
void	 qefree(qent_t *);

typedef struct server {
	char			*se_name;
	char 	 		*se_port;
	char			*se_host;
	char			*se_send_to;

	hostlist_t		 se_accept_from;
	struct addrinfo		*se_accept_addrs;
	hostlist_t		 se_exclude;
	int			 se_resolving;
	struct addrinfo		*se_resolvelist;

	filter_list_t		 se_filters_in,
				 se_filters_out;
	strlist_t		 se_offer_filters;

	struct feeder		*se_feeder;
	time_t			 se_feeder_last_fail;

	DB			*se_q,
				*se_deferred;
	spool_pos_t		 se_pos;

	int			 se_nconns,
				 se_maxconns_in,
				 se_maxconns_out;
	size_t			 se_max_size;

	struct sockaddr_in	 se_bind_v4;
	struct sockaddr_in6	 se_bind_v6;

	uint64_t		 se_in_accepted,
				 se_in_deferred,
				 se_in_refused,
				 se_in_rejected,
				 se_out_accepted,
				 se_out_deferred,
				 se_out_refused,
				 se_out_rejected;

	uint64_t		 se_in_accepted_last,
				 se_in_deferred_last,
				 se_in_refused_last,
				 se_in_rejected_last,
				 se_out_accepted_last,
				 se_out_deferred_last,
				 se_out_refused_last,
				 se_out_rejected_last;

	double			 se_in_accepted_persec,
				 se_in_deferred_persec,
				 se_in_refused_persec,
				 se_in_rejected_persec,
				 se_out_accepted_persec,
				 se_out_deferred_persec,
				 se_out_refused_persec,
				 se_out_rejected_persec;

	int			 se_adp_hi,
				 se_adp_lo;
	char			*se_username_in,
				*se_username_out;

	int			 se_buffer;

	client_list_t		 se_clients;

	SLIST_ENTRY(server)	 se_list;
} server_t;

typedef SLIST_HEAD(server_list, server) server_list_t;
extern server_list_t servers;

int		 server_init(void);
int		 server_run(void);
void		 server_shutdown(void);

server_t	*server_find_by_address(struct sockaddr_storage *);
void		 server_add_backlog(server_t *, struct article *, DB_TXN *);
void		 server_set_spool_pos(server_t *, spool_pos_t *);
int		 server_wants_article(server_t *, article_t *art);
int		 server_accept_offer(server_t *, char const *);
int		 server_has_backlog(server_t *);
void		 server_notify_article(article_t *);
void		 server_defer(server_t *, qent_t *);
void		 server_remove_q(server_t *, qent_t *);
void		 server_addq(server_t *, struct article *, DB_TXN *);

#endif	/* !NTS_SERVER_H */

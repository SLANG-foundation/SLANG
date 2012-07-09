/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

/**
 * \file   net.c
 * \brief  Wrapped network functions, such as 'receive with timestamp'
 * \author Anders Berggren <anders@halon.se>
 * \author Lukas Garberg <lukas@spritelink.net>
 * \date   2010-12-01
 */

#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <netdb.h>
#include "probed.h"
#include "tstamp.h"
#include "net.h"

static int dscp_extract(struct msghdr *msg, /*@out@*/ uint8_t *dscp_out);

/**
 * Receive on socket 'sock' into struct pkt with timestamp
 *
 * Wraps the recv() function, but optimized for the struct pkt_t. The 
 * function receives DATALEN bytes, and places both address, data and
 * RX timestamp into the struct pkt.
 * \param[in]  sock  The socket to read from
 * \param[in]  flags Flags to recv(), usually for reading from error queue
 * \param[out] pkt   Pointer to pkt, where addr, data and tstamp is placed
 * \warning          This function only receices DATALEN bytes
 * \bug              We don't really take care of endianness __at__all__
 */

int recv_w_ts(int sock, int flags, /*@out@*/ pkt_t *pkt) {
	socklen_t addrlen;
	struct msghdr msg[1];
	struct iovec iov[1];
	struct {
		struct cmsghdr cm;
		char control[512];
	} control;

	/* prepare message structure */
	memset(&(pkt->data), 0, DATALEN);
	memset(msg, 0, sizeof *msg);
	addrlen = (socklen_t)sizeof pkt->addr;
	char da[30];
	if ((flags & MSG_ERRQUEUE) != 0) {
		iov[0].iov_base = da;
	} else {
		iov[0].iov_base = pkt->data;
	}
	iov[0].iov_len = DATALEN;
	msg[0].msg_iov = iov;
	msg[0].msg_iovlen = 1;
	msg[0].msg_name = (caddr_t)&pkt->addr;
	msg[0].msg_namelen = addrlen;
	msg[0].msg_control = &control;
	msg[0].msg_controllen = sizeof control;

	if (recvmsg(sock, msg, flags|MSG_DONTWAIT) != DATALEN) {
		/* Recv error! Don't warn about err queue, it's non-block */
		//if ((flags & MSG_ERRQUEUE) == 0)
		//	syslog(LOG_INFO, "recvmsg: %s", strerror(errno));
		return -1;
	} else {
		/* OK, we got a packet */
		if ((flags & MSG_ERRQUEUE) != 0) {
			//data_t *d;
			//d = (data_t *)&pkt->data;
			//d = (data_t *)da;
			//printf("send: %d\n", d->seq);
			//printf("tx2: %d\n", da[4]);
			/* THIS IS A TX TIMESTAMP: store tx tstamp */
			if (tstamp_extract(msg, &pkt->ts, 1) < 0)
				return -1;
			return 0;
		} else {
			//printf("recv: %d\n", pkt->data[1]);
			/* THIS IS NORMAL PACKET: store rx tstamp and DSCP */
			if (tstamp_extract(msg, &pkt->ts, 0) < 0)
				syslog(LOG_ERR, "recv_w_ts: RX tstamp error");
			if (dscp_extract(msg, &pkt->dscp) < 0)
				syslog(LOG_ERR, "recv_w_ts: DSCP error");

			return 0;
		}
	}
}

/**
 * Send 'data' to 'addr'  on socket 'sock' with timestamp
 *
 * Wraps the send() function, but optimized for SLA-NG. It sends DATALEN
 * bytes ('data' has to be that large) onto 'sock' and places the TX 
 * timestamp in 'ts'.
 * \param[in]  sock  The socket to read from
 * \param[in]  addr  Pointer to address where to send the data
 * \param[in]  data  Pointer to the data to send
 * \param[out] ts    Pointer to ts, where to put the TX timestamp
 * \warning          This function only send DATALEN bytes
 * \bug              Will report TX timestamp error if sending data to other 
 *                   interface than the one SO_TIMESTAMPING is active on.
 */

int send_w_ts(int sock, addr_t *addr, char *data, /*@out@*/ ts_t *ts) {
	socklen_t slen;
	
	//printf("tx: %d\n", data[4]);
	memset(ts, 0, sizeof *ts);
	/* get userland tx timestamp (before send, hehe) */
	if (cfg.ts == USERLAND)   
		(void)clock_gettime(CLOCK_REALTIME, ts);
	/* do the send */
	slen = (socklen_t)sizeof *addr;
	if (sendto(sock, data, DATALEN, 0, (struct sockaddr*)addr, slen) < 0) {
		syslog(LOG_INFO, "sendto: %s", strerror(errno));
		return -1;
	}
	/* get kernel tx timestamp */
	if (cfg.ts != USERLAND) { 
		if (tstamp_fetch_tx(sock, ts) < 0) {
			syslog(LOG_ERR, "send_w_ts: TX tstamp error");
			return -1;
		}
	}
	return 0;
}

/**
 * Bind two listening sockets, one UDP (ping/pong) and one TCP (timestamps)
 * 
 * \param[out] s_udp Pointer to UDP socket to create and bind
 * \param[out] s_tcp Pointer to TCP socket to create, bind and listen
 * \param[in]  port  The port number to use for binding
 * \warning          Should be run only once
 */

void bind_or_die(/*@out@*/ int *s_udp, /*@out@*/ int *s_tcp, char *port) {

	int f = 0;
	int ret = 0;
	socklen_t slen;
	struct addrinfo hints, *dst_addrinfo;

	/* UDP socket */
	*s_udp = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (*s_udp < 0) {
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		exit(EXIT_FAILURE);
	} 

	/* Give us a dual-stack (ipv4/6) socket */
	slen = (socklen_t)sizeof f;
	if (setsockopt(*s_udp, IPPROTO_IPV6, IPV6_V6ONLY, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: IPV6_V6ONLY: %s", strerror(errno));

	/* Enable reading of TOS & TTL on received packets */
	f = 1;
	if (setsockopt(*s_udp, IPPROTO_IP, IP_RECVTOS, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: IP_RECVTOS: %s", strerror(errno));
	f = 60;
	if (setsockopt(*s_udp, IPPROTO_IP, IP_RECVTTL, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: IP_RECVTTL: %s", strerror(errno));
	f = 1;
	if (setsockopt(*s_udp, IPPROTO_IPV6, IPV6_RECVTCLASS, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: IPV6_RECVTCLASS: %s",
				strerror(errno));

	/* TCP socket */
	*s_tcp = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (*s_tcp < 0) {
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		exit(EXIT_FAILURE);
	} 

	/* Give us a dual-stack (ipv4/6) socket */
	f = 0;
	slen = (socklen_t)sizeof f;
	if (setsockopt(*s_tcp, IPPROTO_IPV6, IPV6_V6ONLY, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: IPV6_V6ONLY: %s", strerror(errno));
	f = 1;
	if (setsockopt(*s_tcp, SOL_SOCKET, SO_REUSEADDR, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: SO_REUSEADDR: %s",
				strerror(errno));

	/* Prepare for binding ports */
	syslog(LOG_INFO, "Binding port %s", port);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET6;
	hints.ai_flags = (AI_V4MAPPED | AI_PASSIVE);
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	/* Perform getaddrinfo */
	ret = getaddrinfo(NULL, port, &hints, &dst_addrinfo);
	if (ret < 0) {
		syslog(LOG_ERR, "Unable to bind: %s", gai_strerror(ret));
		exit(EXIT_FAILURE);
	}

	/* Bind! */
	if (bind(*s_udp, dst_addrinfo->ai_addr, dst_addrinfo->ai_addrlen) < 0) {
		syslog(LOG_ERR, "bind: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (bind(*s_tcp, dst_addrinfo->ai_addr, dst_addrinfo->ai_addrlen) < 0) {
		syslog(LOG_ERR, "bind: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (listen(*s_tcp, 10) == -1) {
		syslog(LOG_ERR, "listen: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/**
 * Set DSCP-value of socket.
 *
 * \param[in] sock Socket
 * \param[in] dscp DSCP value 
 * \return Status; 0 on successs, <0 on failure.
 */
int dscp_set(int sock, uint8_t dscp) {
	socklen_t slen;
	int tclass = 0;

	/* Remove ECN */
	dscp <<= 2;
	/* IPv6 TCLASS */
	slen = (socklen_t)sizeof tclass;
	tclass = (int)dscp;
	if (setsockopt(sock, IPPROTO_IPV6, IPV6_TCLASS, &tclass, slen) < 0) {
		syslog(LOG_ERR, "setsockopt: IPV6_TCLASS: %s", strerror(errno));
		return -1;
	}
	/* IPv4 TOS */
	slen = (socklen_t)sizeof dscp;
	if (setsockopt(sock, IPPROTO_IP, IP_TOS, &dscp, slen) < 0) {
		syslog(LOG_ERR, "setsockopt: IP_TOS: %s", strerror(errno));
		return -1;
	}
	return 0;
}

/**
 * Extracts the DSCP value from a packet.
 *
 * \param[in] msg Pointer to the message's header data.
 * \param[out] dscp_out Pointer to location where the DSCP will be written.
 *
 * \todo Find out if we need to check cmsg_len (as http://stackoverflow.com/questions/2881200/linux-can-recvmsg-be-used-to-receive-the-ip-tos-of-every-incoming-packet shows).
 * \todo Fix splint branchstate ignore
 */
static int dscp_extract(struct msghdr *msg, /*@out@*/ uint8_t *dscp_out) {

	struct cmsghdr *cmsg;
	int *ptr;
	uint8_t tos = 255;
	
	*dscp_out = 0;
	/* iterate cmsg headers, look for IP_TOS */
	/*@ -branchstate Don't care about cmsg storage */
	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		/* Fetch TOS value */
		if (cmsg->cmsg_level == IPPROTO_IP && 
			cmsg->cmsg_type == IP_TOS) { 
			ptr = (int *)CMSG_DATA(cmsg);
			tos = (uint8_t)*ptr;
			/* Discard the two righmost bits (ECN) */
			*dscp_out = tos >> 2;
			return 0;
		}
		/* Fetch TCLASS value (DSCP) */
		if (cmsg->cmsg_level == IPPROTO_IPV6 && 
			cmsg->cmsg_type == IPV6_TCLASS) {
			ptr = (int *)CMSG_DATA(cmsg);
			tos = (uint8_t)*ptr;
			/* Discard the two righmost bits (ECN) */
			*dscp_out = tos >> 2;
			return 0;
		}
	}
	/*@ +branchstate */
	return -1;
}

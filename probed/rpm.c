/*
 * Juniper RPM compliant ping
 * 
 * Lukas Garberg <lukas@spritelink.net> & Anders Berggren <anders@halon.se>
 * 
 */

#include <netinet/in.h>
#include "sla-ng.h"


void rpm() {
	struct timeval tv;
	
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		perror("setsockopt: SO_RCVTIMEO");
	while (1) {
		them.sin_family = AF_INET;
		them.sin_port = htons(c.port);
		if (inet_aton("10.10.0.2", &them.sin_addr) < 0)
			perror("inet_aton: Check the IP address");
		rpm_fsm();
		usleep(100000);
	}
	close(s);
}

void rpm_fsm() {

	struct timespec rx, tx, rrx, rtx, di, ldi, rtt;
	struct rpm_packet pkt;
	struct rpm_packet *pkt_in;
	
	pkt.t1_sec = 0;
	pkt.t1_usec = 0;
	pkt.t2_sec = 0;
	pkt.t2_usec = 0;
	pkt.t3_sec = 0;
	pkt.t3_usec = 0;
	pkt.t4_sec = 0;
	pkt.t4_usec = 0;
	pkt.version = htons(1);
	pkt.magic = htons(0x9610);
	pkt.reserved = 0;

	if (c.debug) printf("TX bits: %X %X %X %X \n", 
			pkt.t1_sec, pkt.version, pkt.t3_usec, pkt.magic);

	data_send((char*)&pkt, sizeof(pkt)); /* send ping */
	tx = p.ts; /* save timestamp */
	
	data_recv(0); /* wait for pong */
	rx = p.ts; /* save timestamp */

	pkt_in = (struct rpm_packet*)&p.data;
	if (c.debug) printf("RX stamps: t3: %d.%d t2: %d.%d \n", 
			ntohl(pkt_in->t2_sec), ntohl(pkt_in->t2_usec), 
			ntohl(pkt_in->t3_sec), ntohl(pkt_in->t3_usec));

	rrx.tv_sec = ntohl(pkt_in->t3_sec);
	rrx.tv_nsec = ntohl(pkt_in->t3_usec)*1000;
	rtx.tv_sec = ntohl(pkt_in->t2_sec);
	rtx.tv_nsec = ntohl(pkt_in->t2_usec)*1000; 

	diff_ts(&di, &rrx, &rtx);
	diff_ts(&ldi, &rx, &tx);
	diff_ts(&rtt, &ldi, &di);
	
	if (c.debug) printf("DI %010ld.%09ld LDI %010ld.%09ld\n", 
			di.tv_sec, di.tv_nsec, ldi.tv_sec, ldi.tv_nsec);
	printf("RTT %010ld.%09ld\n", rtt.tv_sec, rtt.tv_nsec);
}

/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

void bind_or_die(/*@out@*/ int *s_udp, /*@out@*/ int *s_tcp, uint16_t port);
void loop_or_die(int s_udp, int s_tcp, char *port, char *cfgpath);

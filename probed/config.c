/*
 * XML CONFIG
 * Author: Anders Berggren
 *
 * Function config_read()
 * Handles the XML "settings" config file. Things I've learnt about libxml2 that
 * might be good to know, is that everything returned by xml* functions have to
 * be free:ed, such as freeing the string returned from xmlNodeGetContent with
 * xmlFree. Also, the best way to just get something is by using XPath (getkey)
 * while the simplest way to read though the whole thing is to loop though the
 * document with for(n=doc->children;n!=0;n=n->next). 
 */

#include "sla-ng.h"

xmlDoc *doc;
const char *cfgpath = "settings.xml";
struct cfg c;

void config_read() {
	char tmp[TMPLEN];
	xmlDoc *tmpdoc;

	/* reload configuration */
	tmpdoc = xmlParseFile(cfgpath);
	if (!doc) {
		syslog(LOG_ERR, "Invalid configuration. (xmlParseFile)");
		return;
	}
	xmlFreeDoc(doc);
	doc = tmpdoc;
	/* default config */
	c.debug = 1; /* extra output */
	c.port = 0; /* server port */
	c.ts = 'u'; /* timestamping mode (s)w (k)ern (h)w */
	/* configuration application */
	syslog(LOG_INFO, "Reloading configuration...");
	/* timestamping mode and interface */
	config_getkey("/config/interface", c.iface, sizeof(c.iface)); 
	if (config_getkey("/config/timestamp", tmp, TMPLEN) < 0)
	       tstamp_hw(); /* default is hw with fallback */
	else {
		if (tmp[0] == 'k') tstamp_kernel();
		else if (tmp[0] == 's') tstamp_sw();
		else tstamp_hw();
	}
	/* server port */
	if (config_getkey("/config/port", tmp, TMPLEN) == 0)
		proto_bind(atoi(tmp));
	/* extra output */
	if (config_getkey("/config/debug", tmp, TMPLEN) == 0) {
		if (tmp[0] == 't' || tmp[0] == '1') {
			setlogmask(LOG_UPTO(LOG_DEBUG));
			c.debug = 1;
		} else {
			setlogmask(LOG_UPTO(LOG_INFO));
			c.debug = 0;
		}
	}
	config_scan();
}

void config_root(xmlNode *n, char *c) {

/*	if (strncmp((char*)n->name, "ping", 4) == 0) {
		printf("CP: %s\n", c); 
	}*/
}

void config_scan() {
	xmlNode *n, *r;
	xmlChar *c;

	if (!doc->children) {
		syslog(LOG_ERR, "Empty configuration.");
		return;
	}
	r = doc->children;
	for (n = r->children; n != 0; n = n->next) {
		if (n->type != XML_ELEMENT_NODE) continue;
		c = xmlNodeGetContent(n);
		config_root(n, (char *)c);
		xmlFree(c);
	} 
}

int config_getkey(char *xpath, char *str, size_t bytes) {
	xmlXPathContext *ctx; 
	xmlXPathObject *o;
	xmlNodeSet *set;	
	xmlNode *n;
	xmlChar *data;

	ctx = xmlXPathNewContext(doc);
	if (!ctx) {
		syslog(LOG_ERR, "xmlXPathNewContext: %s", strerror(errno));
		return -1;
	}
	o = xmlXPathEvalExpression((xmlChar*)xpath, ctx);
	if (!o) {
		syslog(LOG_ERR, "xmlXPathEvalExpression: %s", strerror(errno));
		xmlXPathFreeContext(ctx);
		return -1;
	}
	set = o->nodesetval;
	if (xmlXPathNodeSetIsEmpty(set)) {
		xmlXPathFreeObject(o);
		xmlXPathFreeContext(ctx);
		return -1;
	}
	n = set->nodeTab[0];    
	data = xmlNodeGetContent(n);
	strncpy(str, (char *)data, bytes-1);
	str[bytes-1] = '\0';
	xmlFree(data);
	xmlXPathFreeObject(o);
	xmlXPathFreeContext(ctx);
	return 0;
}

void config_init() {
	doc = xmlParseFile(cfgpath);
	if (!doc) die("Invalid configuration. (xmlParseFile)");
}


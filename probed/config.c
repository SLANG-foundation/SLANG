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

#include "probed.h"
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

int config_read(xmlDoc **doc, char *cfgpath) {
	xmlDoc *tmpdoc;

	/* reload configuration file */
	tmpdoc = xmlParseFile(cfgpath);
	if (!tmpdoc) {
		syslog(LOG_ERR, "Invalid configuration. (xmlParseFile)");
		return -1;
	}
	xmlFreeDoc(*doc);
	*doc = tmpdoc;
	return 0;
}

int config_getkey(xmlDoc *doc, char *xpath, char *str, size_t bytes) {
	xmlXPathContext *ctx; 
	xmlXPathObject *o;
	xmlNodeSet *set;	
	xmlNode *n;
	xmlChar *data;

	if (doc == 0) {
		syslog(LOG_ERR, "No configuration.");
		return -1;
	}
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

/*
 * Sync measurement sessions from config to msess
 */
int config_msess(xmlDoc *doc) {
	xmlNode *n, *r, *k;
	/*xmlAttr *a;*/
	xmlChar *c;
	struct sockaddr_in6 addr;
	struct msess *sess;

	if (doc == 0) {
		syslog(LOG_ERR, "No configuration.");
		return -1;
	}
	if (!doc->children) {
		syslog(LOG_ERR, "Empty configuration.");
		return -1;
	}
	/* iterate config - look for <probe> */
	r = doc->children;
	for (n = r->children; n != NULL; n = n->next) {

		if ( !(
			n->type == XML_ELEMENT_NODE && 
			( strncmp((char *)n->name, MSESS_NODE_NAME, strlen(MSESS_NODE_NAME)) == 0 ) 
			) ) continue; 
		
		/* <probe> found - reset */
		sess = malloc(sizeof (struct msess));
		memset(sess, 0, sizeof (struct msess));
		memset(&addr, 0, sizeof addr);
		addr.sin6_family = AF_INET6;

		/* get ID */
		c = xmlGetProp(n, (xmlChar *)"id");
		if (c != NULL) {
			printf("  id: %s\n", c);
			sess->id = atoi((char *)c);
		} else {
			syslog(LOG_ERR, "Found probe withour ID");
			continue;
		}
		xmlFree(c);

		/* get child nodes */
		for (k = n->children; k != NULL; k = k->next) {

			if (k->type != XML_ELEMENT_NODE) {
				continue;
			}

			c = xmlNodeGetContent(k);
			printf("  Child name: %s content: %s\n", (char *)k->name, c);

			/* interval */
			if (strcmp((char *)k->name, "interval") == 0) {
				sess->interval.tv_usec = atoi((char *)c);
				printf("   Got interval %d\n", (int)sess->interval.tv_usec);
			}
			
			/* address */
			if (strcmp((char *)k->name, "address") == 0) {
				inet_pton(AF_INET6, (char *)c, &addr.sin6_addr.s6_addr);
				printf("   Got address %s\n", c);
			}

			/* dscp */
			if (strcmp((char *)k->name, "dscp") == 0) {
				sess->dscp = atoi((char *)c);
				printf("   Got dscp %s\n", c);
			}

			/* port */
			if (strcmp((char *)k->name, "port") == 0) {
				addr.sin6_port = htons(atoi((char *)c));
				printf("   Got port %d\n", (int)sess->interval.tv_usec);
			}

			xmlFree(c);
			
		}

		memcpy(&sess->dst, &addr, sizeof addr);
		msess_add_or_update(sess);
		c = xmlNodeGetContent(n);
		printf(" Content: %s\n", c);
		xmlFree(c);
	}
	return 0;
}


/**
 * Handles XML config file.
 *
 * Handles the XML "settings" config file. Things I've learnt about libxml2 that
 * might be good to know, is that everything returned by xml* functions have to
 * be free:ed, such as freeing the string returned from xmlNodeGetContent with
 * xmlFree. Also, the best way to just get something is by using XPath (getkey)
 * while the simplest way to read though the whole thing is to loop though the
 * document with for(n=doc->children;n!=0;n=n->next). 
 *
 * \file config.c
 * \author Anders Berggren <anders@halon.se>
 * \author Lukas Garberg <lukas@spritelink.net>
 */


#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include "probed.h"
#include "config.h"
#include "msess.h"

xmlDoc *cfgdoc = 0;

/**
 * Reads configuration from disk.
 *
 * \param[in] cfgpath Path to config file.
 * \return Status; 0 on success, <0 on failure.
 */
int config_read(char *cfgpath) {

	xmlDoc *tmpdoc;

	/* read configuration file */
	tmpdoc = xmlParseFile(cfgpath);
	if (!tmpdoc) 
		return -1;

	xmlFreeDoc(cfgdoc);
	cfgdoc = tmpdoc;
	return 0;

}

/**
 * Fetches configuration parameter from config.
 *
 * \param[in] xpath XPath string for the config key.
 * \param[out] str Pointer to a buffer where the result will be written.
 * \param[in] bytes Size of buffer.
 * \return Status of execution. 0 on success, <0 on failure.
 */
int config_getkey( char *xpath, char *str, size_t bytes) {

	xmlXPathContext *ctx; 
	xmlXPathObject *o;
	xmlNodeSet *set;	
	xmlNode *n;
	xmlChar *data;

	if (cfgdoc == 0) {
		syslog(LOG_ERR, "No configuration.");
		return -1;
	}
	ctx = xmlXPathNewContext(cfgdoc);
	if (!ctx) {
		syslog(LOG_ERR, "xmlXPathNewContext: %s", strerror(errno));
		return -1;
	}

	/* find key */
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

/**
 * Sync measurement sessions from config to msess.
 *
 * Syncs measurement session list to configuration file. New sessions
 * are added, old sessions updated and removed sessions removed.
 *
 * \return Status; 0 on success, <0 on failure.
 *
 * \todo Does not yet remove sessions which does not exist.
 */
int config_msess() {

	xmlNode *n, *r, *k;
	xmlChar *c;
	char port[TMPLEN];
	struct msess *sess;
	struct addrinfo /*@dependent@*/ dst_hints, *dst_addr;
	int ret_val = 0;
	
	/* configuration sanity checks */
	if (cfgdoc == 0) {
		syslog(LOG_ERR, "No configuration.");
		return -1;
	}
	if (!cfgdoc->children) {
		syslog(LOG_ERR, "Empty configuration.");
		return -1;
	}

	/* get port */
	if (config_getkey("/config/port", port, TMPLEN) != 0) {
		syslog(LOG_CRIT, "Unable to get port from configuration");
		return -1;
	}

	/* iterate config - look for <probe> */
	r = cfgdoc->children;
	for (n = r->children; n != NULL; n = n->next) {

		if ( !(
			n->type == XML_ELEMENT_NODE && 
			( strncmp((char *)n->name, MSESS_NODE_NAME, strlen(MSESS_NODE_NAME)) == 0 ) 
			) ) continue; 
		
		/* <probe> found - reset temporary varsiables and create new msess */
		sess = malloc(sizeof (struct msess));
		memset(sess, 0, sizeof (struct msess));

		/* get ID */
		c = xmlGetProp(n, (xmlChar *)"id");
		if (c != NULL) {
			sess->id = atoi((char *)c);
		} else {
			syslog(LOG_ERR, "Found probe without ID");
			continue;
		}
		xmlFree(c);

		/* get child nodes */
		for (k = n->children; k != NULL; k = k->next) {

			if (k->type != XML_ELEMENT_NODE) {
				continue;
			}

			c = xmlNodeGetContent(k);

			/* interval */
			if (strcmp((char *)k->name, "interval") == 0) {
				sess->interval.tv_usec = atoi((char *)c);
			}
			
			/* address */
			if (strcmp((char *)k->name, "address") == 0) {

				/* prepare for getaddrinfo */
				memset(&dst_hints, 0, sizeof dst_hints);
				dst_hints.ai_family = AF_INET6;
				dst_hints.ai_flags = AI_V4MAPPED;

				ret_val = getaddrinfo((char *)c, port, &dst_hints, &dst_addr);
				if (ret_val < 0) {
					syslog(LOG_ERR, "Unable to look up hostname %s: %s", (char *)c, gai_strerror(ret_val));
				}

			}

			/* dscp */
			if (strcmp((char *)k->name, "dscp") == 0) {
				sess->dscp = atoi((char *)c);
			}

			xmlFree(c);
			
		}

		memcpy(&sess->dst, dst_addr->ai_addr, sizeof sess->dst);
		freeaddrinfo(dst_addr);

		msess_add_or_update(sess);

	}

	return 0;

}

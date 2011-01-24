/*
 * Test for config module.
 */
#include <stdio.h>
#include <stdlib.h>

#include "probed.h"
#include "msess.h"

int main(int argc, char *argv[]) {

	xmlDoc *cfgdoc = 0;
	char *cfgpath;

	cfgpath = "../settings.xml";
	config_read(&cfgdoc, cfgpath);

	msess_init();
	config_msess(cfgdoc);
	msess_print_all();

	return 0;

}

#! /usr/bin/python

import sys
import os
import logging
import threading
import signal
from twisted.web import xmlrpc, server
from twisted.internet import reactor

import config
import probestore
import maintainer
import probed
import remoteproc

class Manager:

    logger = None
    config = None
    server = None
    pstore = None
    maintainer = None

    thread_stop = False

    def __init__(self):
        """ Constructor """

        self.config = config.Config()
        self.logger = logging.getLogger(self.__class__.__name__)

        try:
            self.pstore = probestore.ProbeStore()
            self.maintainer = maintainer.Maintainer(self.pstore)
            self.probed = probed.Probed(self.pstore)

            # Create XML-RPC server
            self.xmlrpc = remoteproc.RemoteProc(self.pstore, self)
            reactor.listenTCP(8000, server.Site(self.xmlrpc))

        except Exception, e:
            self.logger.critical("Cannot start: %s" % e)
            self.stop()

    def reload(self):
        """ Reload 

           Fetches configuration from central node and saves to disk.
           Then, send a SIGHUP to the probe application to make it reload
           the configuration.
        """

        self.logger.info("Reloading...")

        # fetch config

        # write to disk

        # send SIGHUP
        self.probe.send_signal(SIGHUP)

        return 1;

    def sighandler(self, signum, frame):
        """ Signal handler. """

        if signum == signal.SIGINT or signum == signal.SIGALRM or signum == signal.SIGTERM:
            self.stop()

    def stop(self):
        """ Stop everything """

        self.logger.info("Stopping all threads...")

        # stop threads
        try:
            self.maintainer.stop()
        except:
            pass

        try:
            self.probed.stop()
        except:
            pass

        try:
            self.pstore.stop()
        except:
            pass

        try:
            reactor.stop()
        except:
            pass

        # wait for threads to finish...
        self.logger.debug("Waiting for maintainer...")
        try:
            self.maintainer.join()
        except:
            pass

        self.logger.debug("Maintainer done. Waiting for probed...")

        try:
            self.probed.join()
        except:
            pass

        self.logger.debug("Probed done.")

    def run(self):
        """ Start the application """

        self.logger.info("Starting execution")
        
        # start threads
        self.probed.start()
        self.maintainer.start()

        reactor.run()

        self.logger.info("Exiting...")

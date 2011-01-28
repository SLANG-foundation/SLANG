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
        self.pstore = probestore.ProbeStore()
        self.probed = probed.Probed(self.pstore)
        self.maintainer = maintainer.Maintainer(self.pstore)

        # Create XML-RPC server
        self.xmlrpc = remoteproc.RemoteProc(self.pstore, self)
        reactor.listenTCP(8000, server.Site(self.xmlrpc))

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

        if signum == signal.SIGINT or signum == signal.SIGKILL or signum == signal.SIGALRM or signum == signal.SIGTERM:
            self.stop()

    def stop(self):
        """ Stop everything """

        self.logger.info("Stopping all threads...")

        # stop threads
        self.maintainer.stop()
        self.probed.stop()
        self.thread_stop = True

        reactor.stop()

        # wait for threads to finish...
        self.logger.debug("Waiting for maintainer...")
        self.maintainer.join()
        self.logger.debug("Maintainer done. Waiting for probed...")
        self.probed.join()
        self.logger.debug("Probed done.")
#        del self.pstore
#        self.logger.debug("pstore dead.")

    def run(self):
        """ Start the application """

        self.logger.info("Starting execution")
        
        # start threads
        self.probed.start()
        self.maintainer.start()

        reactor.run()

        self.logger.info("Exiting...")

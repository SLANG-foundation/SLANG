#! /usr/bin/python

import subprocess
import sys
import logging
import threading
#from struct import unpack 
import time

from probe import Probe
import config

class Probed(threading.Thread):
    """ Manages probed
    """

    fifo = None
    logger = None
    config = None
    pstore = None
    thread_stop = False

    def __init__(self, pstore):

        threading.Thread.__init__(self)

        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config.Config()

        self.pstore = pstore

        # start probe application
        try:
            # \todo - Redirect to /dev/null!
            self.probe = subprocess.Popen(['../probed/probed', '-i', self.config.get_param("/config/interface"), '-d'], 
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False)
            self.logger.debug('Probe application started')
        except Exception, e:
            self.logger.critical("Unable to start probe application: %s" % e)
            raise ProbedError("Unable to start probed application: %s" % e)

        # Try to open fifo
        while self.fifo is None:
            time.sleep(1)
            self.open_fifo()

    def open_fifo(self):
        try:
            self.fifo = open(self.config.get_param("/config/fifopath"), 'r');
        except Exception, e:
            self.logger.critical("Unable to open fifo: %s" % e)

    def stop(self):
        """ Stop thread execution """
        self.thread_stop = True

    def run(self):
        """
        Function which is run when thread is started.
        
        Will infinitely read from fifo. 

        """
        
        while True:

            if self.thread_stop: 
                self.logger.info("Stopping thread...")
                break

            if self.fifo.closed:
                self.logger.warn("FIFO closed. Retrying...")
                self.open_fifo()
                time.sleep(1)
                continue

            try:
                # ~780 probes can be held in fifo buff before pause
                data = self.fifo.read(128)
#                self.logger.debug("got ipc: %d bytes " % len(data))

                # error condition - handle in nice way!
                # \todo Handle read from dead fifo in a nice way.
                if len(data) < 1:
                    continue

                p = Probe(data)
                self.pstore.insert(p)

            except Exception, e:
                self.logger.error("Unable to read from FIFO: %s" % e)
                time.sleep(1) 

    def __del__(self):
        self.thread_stop = True
        self.fifo.close()

class ProbedError(Exception):
    pass

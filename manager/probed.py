#! /usr/bin/python

import subprocess
import sys
import logging
import threading
import time

import probe
import config

class Probed(threading.Thread):
    """ Manages probed
    """

    probed = None
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
        self.null = open("/dev/null", 'w')

        # start probe application
        self.start_probed()

        # Try to open fifo
        while self.fifo is None:
            self.logger.debug("Waiting for FIFO...")
            time.sleep(1)
            self.open_fifo()

    def start_probed(self):
        """ Start probed application """
        try:
            # \todo - Redirect to /dev/null!
            self.probed = subprocess.Popen(['../probed/probed', '-i', self.config.get_param("/config/interface"), '-d', '-q'], 
                stdout=self.null, stderr=self.null, shell=False)
            self.logger.debug('Probe application started, pid %d', self.probed.pid)
        except Exception, e:
            self.logger.critical("Unable to start probe application: %s" % e)
            raise ProbedError("Unable to start probed application: %s" % e)

        time.sleep(1)
        if self.probed.poll() != None:
            raise ProbedError("Unable to start probed application")

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

            # check if probed is alive
            if self.probed.poll() != None:
                self.logger.warning("probed not running!")
                self.fifo.close()
                self.start_probed()
                continue

            if self.fifo.closed:
                self.logger.warn("FIFO closed. Retrying...")
                time.sleep(1)
                self.open_fifo()
                continue

            try:
                # ~780 probes can be held in fifo buff before pause
                data = self.fifo.read(128)
#                self.logger.debug("got ipc: %d bytes " % len(data))
            except Exception, e:
                self.logger.error("Unable to read from FIFO: %s" % e)
                time.sleep(1) 

            # error condition - handle in nice way!
            # \todo Handle read from dead fifo in a nice way.
            if len(data) < 1:
                continue

            try:
            p = probe.from_struct(data)
            self.pstore.add(p)
            except Exception, e:
                self.logger.error("Unable to add probe, got %s: %s" % (e.__class__.__name__, e, ))

        self.fifo.close()

class ProbedError(Exception):
    pass

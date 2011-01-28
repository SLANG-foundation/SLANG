#! /usr/bin/python

import subprocess
import sys
import logging
import threading
#from struct import unpack 
from probe import Probe

import config

class Probed(threading.Thread):
    """ Manages probed
    """

    fifo = None
    logger = None
    config = None
    pstore = None

    def __init__(self, pstore):

        threading.Thread.__init__(self)

        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config.Config()

        self.pstore = pstore

        # start probe application
        try:
            # \todo - Redirect to /dev/null!
            self.probe = subprocess.Popen(['../probed/probed', '-d'], 
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False)
            self.logger.debug('Probe application started')
        except :
            self.logger.critical("Unable to start probe application!")
            raise ProbedError("Unable to start probed application!")

        # open fifo
        try:
            self.fifo = open(self.config.get_param("/config/fifopath"), 'r');
        except Exception, e:
            self.logger.critical("Unable to open fifo: %s" % e)
            raise ProbedError("Unable to open fifo: %s" % e)

    def run(self):
        """
        Function which is run when thread is started.
        
        Will infinitely read from fifo. 

        """
        
        while True:
            try:
                data = self.fifo.read(128)
                self.logger.debug("got ipc: %d bytes " % len(data))
                # it can hold 2500 probes in fifo buff before pause
                p = Probe(data)
#                data = unpack('llc16siillllllll16s', data)
                self.pstore.insert(p)
            except:
                print('lost ipc')
                raise
                time.sleep(1) 

        self.fifo.close()

class ProbedError(Exception):
    pass

import threading
from time import time, sleep
import logging

import probestore
import config

class Maintainer(threading.Thread):
    """ Performs maintenance operations at regular intervals. """

    flush_interval = 10
    delete_interval = 600
    
    logger = None
    pstore = None
    thread_stop = False

    last_flush = None
    last_delete = None

    def __init__(self, pstore):

        threading.Thread.__init__(self, name=self.__class__.__name__)

        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config.Config()
        
        # save ProbeStorage instance
        self.pstore = pstore

        # initialize time tracking instance variables
        self.last_flush = time()
        self.last_delete = time()
        

    def run(self):
        """ Starts thread. """

        while True:

            if self.thread_stop:
                self.logger.info("Stopping thread...")
                break
            
            if (time() - self.last_flush) >= self.flush_interval:
                self.logger.debug("Starting flush...")
                self.pstore.flush()
                self.last_flush = time()

            if (time() - self.last_delete) >= self.delete_interval:
                self.logger.debug("Starting delete...")
                self.pstore.delete(300)
                self.last_delete = time()

            sleep(1)
            
    def stop(self):
        """ Stop thread """

        self.thread_stop = True

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

    last_flush = None
    last_delete = None

    def __init__(self, pstore):

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
            
            if time.now() - self.last_flush <= self.flush_interval:
                self.pstore.flush()

            if time.now() - self.last_delete <= self.delete_interval:
                self.pstore.delete(600)

            time.sleep(5)
            

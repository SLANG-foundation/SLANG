import threading
from time import time, sleep
import logging

import probestore
import config

class Maintainer(threading.Thread):
    """ Performs maintenance operations at regular intervals. """

    flush_interval = 1
    delete_interval = 600
    reload_interval = 3600
    
    logger = None
    pstore = None
    manager = None
    thread_stop = False

    last_flush = None
    last_delete = None
    last_reload = None

    def __init__(self, pstore, manager):

        threading.Thread.__init__(self, name=self.__class__.__name__)

        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config.Config()
        self.manager = manager
        
        # save ProbeStorage instance
        self.pstore = pstore

        # initialize time tracking instance variables
        self.last_flush = time()
        self.last_delete = time()
        self.last_reload = time()
        

    def run(self):
        """ Starts thread. """

        while True:

            if self.thread_stop:
                break
            
            if (time() - self.last_flush) >= self.flush_interval:
                self.pstore.flush()
                self.last_flush = time()

            if (time() - self.last_delete) >= self.delete_interval:
                self.pstore.delete(7200)
                self.last_delete = time()
            
            if (time() - self.last_reload) >= self.reload_interval:
                try:
                    self.manager.reload()
                except Exception, e:
                    self.logger.error("Maintenace reload operation failed: %s" % str(e))
                self.last_reload = time()

            sleep(1)
            
    def stop(self):
        """ Stop thread """

        self.thread_stop = True

import threading
from time import time, sleep
import logging

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
        self.last_pstore_aggr = ((int(time()) / self.pstore.AGGR_DB_LOWRES) * 
            self.pstore.AGGR_DB_LOWRES + 3*self.pstore.AGGR_DB_LOWRES)
        

    def run(self):
        """ Starts thread. """

        while True:

            if self.thread_stop:
                break
            
            # flush (commit) to database
            if (time() - self.last_flush) >= self.flush_interval:
                self.pstore.flush()
                self.last_flush = time()

            # remove old data from database, 30min HIGHRES and 24h LOWRES
            if (time() - self.last_delete) >= self.delete_interval:
                self.pstore.delete(1800, 3600*24)
                self.last_delete = time()
            
            # reload every hour
            if (time() - self.last_reload) >= self.reload_interval:
                try:
                    self.manager.reload()
                except Exception, e:
                    self.logger.error("Maintenace reload operation failed: %s"
                        % str(e))
                self.last_reload = time()

            # save precalculated aggregates to database
            if (time() - self.last_pstore_aggr) >= self.pstore.AGGR_DB_LOWRES:

                start = self.last_pstore_aggr

                # perform operation for each session we have data for
                sesss = self.pstore.current_sessions()

                for sess in sesss:
                    try:
                        self.pstore.aggregate(sess, (start - self.pstore.AGGR_DB_LOWRES)*1000000000)
                    except Exception, e:
                        self.logger.error("aggregation failed: %s. Missing valid measurement data?" % str(e))
                        continue

                self.last_pstore_aggr = ((int(time()) / self.pstore.AGGR_DB_LOWRES) * 
                    self.pstore.AGGR_DB_LOWRES)

            sleep(1)

            
    def stop(self):
        """ Stop thread """

        self.thread_stop = True

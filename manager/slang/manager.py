#! /usr/bin/python

import urllib2
import sys
import os
import logging
import threading
import signal
import httplib
from twisted.web import xmlrpc, server
from twisted.internet import reactor

import config
import probestore
import maintainer
import probed
import remoteproc

class Manager:

    cfg_path = None

    logger = None
    config = None
    server = None
    pstore = None
    probed = None
    maintainer = None

    thread_stop = False

    def __init__(self, cfg_path):
        """ Constructor """

        # reload
        self.logger = logging.getLogger(self.__class__.__name__)

        self.cfg_path = cfg_path
        self.config = config.Config(self.cfg_path)

        # fetch configuration
        try:
            self.reload()      

        except ManagerError, e:

            # if there is an existing config file, use it
            if os.path.isfile(self.config.get('probed_cfg')):
                self.logger.info("Using existing config")

            else:
                # Otherwise, try to write an empty one for probed
                self.logger.warning("Writing empty config")
                try:
                    f = open(self.config.get('probed_cfg'), 'w')
                    f.write("<config></config>")
                    f.close()
                except IOError, e:
                    self.logger.critical("Unable to write cfg: %s" % str(e))

        try:
            self.pstore = probestore.ProbeStore()
            self.maintainer = maintainer.Maintainer(self.pstore, self)
            self.probed = probed.Probed(self.pstore)

            # Create XML-RPC server
            rpc = remoteproc.RemoteProc(self.pstore, self)
            xmlrpc.addIntrospection(rpc)
            reactor.listenTCP(int(self.config.get('rpcport')), server.Site(rpc))

        except Exception, e:
            self.logger.critical("Cannot start: %s" % e)
            self.stop()


    def reload(self):
        """ Reload 

           Fetches probe configuration from central node and saves to disk.
           Then, send a SIGHUP to the probe application to make it reload
           the configuration.
        """

        self.logger.info('Reloading configuration...')

        try:
            # fetch probed config
            f_cfg = urllib2.urlopen(self.config.get('configurl'), timeout=5)
            cfg_data = f_cfg.read()
            self.write_config(cfg_data)

        except urllib2.URLError, e:
            estr = str('Config fetch error: %s' % str(e))
            self.logger.critical(estr)
            raise ManagerError(estr)

        # send SIGHUP
        if self.probed is not None:
            self.probed.reload()

    
    def write_config(self, cfg_data):
        """ Save config 'cfg_data' to disk and reload application. """

        self.logger.info('Writing "probed" configuration...')

        # diff files
        try:
            probed_cfg_file = open(self.config.get('probed_cfg'), 'r')
            current_cfg_data = probed_cfg_file.read()
            probed_cfg_file.close()
            if current_cfg_data == cfg_data:
                self.logger.info('Config unchanged, not touched')
                return
        except IOError, e:
	    pass

        # write to disk
        try:
            probed_cfg_file = open(self.config.get('probed_cfg'), "w")
            probed_cfg_file.write(cfg_data)
            probed_cfg_file.close()
        except IOError, e:
            estr = str("Unable to write config file: %s" % str(e))
            self.logger.critical(estr)
            raise ManagerError(estr)

        # send SIGHUP
        if self.probed is not None:
            self.probed.reload()

    
    def sighandler(self, signum, frame):
        """ Signal handler. """

        if signum == signal.SIGINT or signum == signal.SIGTERM:
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
        self.logger.debug("Waiting for Maintainer...")
        try:
            self.maintainer.join()
        except:
            pass

        self.logger.debug("Maintainer done. Waiting for Probed...")

        try:
            self.probed.join()
        except:
            pass

        self.logger.debug("Probed done. Waiting for ProbeStore...")

        try:
            self.pstore.join()
        except:
            pass

        self.logger.debug("ProbeStore done.")


    def run(self):
        """ Start the application """

        self.logger.info("Starting execution")
        
        # start threads
        try:
            self.pstore.start()
            self.probed.start()
            self.maintainer.start()
        except Exception, e:
            self.logger.error("Unable to start threads: %s" % str(e))
            self.stop()

        reactor.run()

        self.logger.info("Exiting...")


class ManagerError(Exception):
    pass

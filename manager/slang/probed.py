#! /usr/bin/python

import subprocess
import sys
import logging
import threading
import time
import signal

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

    def __init__(self, pstore, probed_cfg_path):

        threading.Thread.__init__(self)

        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config.Config()

        self.pstore = pstore
        self.null = open("/dev/null", 'w')
        self.probed_cfg_path = probed_cfg_path

        # start probe application
        self.start_probed()

        # Try to open fifo
        while self.fifo is None:
            self.logger.debug("Waiting for FIFO...")
            time.sleep(1)
            self.open_fifo()

    def start_probed(self):
        """ Start probed application """

        probed_args = ['/usr/bin/probed', '-q']

        # get configuration - port
        try:
            port = self.config.get_param('port')
        except NotFoundError:
            port = 60666
            self.logger.warning("Port not found in config. Falling back to default (%d)" % port)

        probed_args += ['-p', port]

        # FIFO path
        try:
            fifo_path = self.config.get_param('fifopath')
        except NotFoundError:
            fifo_path = "/tmp/probed.fifo"
            self.logger.warning("FIFO path not found in config. Falling back to default(%s)" % fifo_path)

        probed_args += ['-d', fifo_path]

        # timestamping type - hardware is default
        try:
            tstype = self.config.get_param('timestamp')
        except NotFoundError:
            tstype = 'userland'
            self.logger.info("Timestamping type not found in config. Falling back to default (%s)" & tstype)

        if tstype == 'kernel':
            probed_args += ['-k']

        elif tstype == 'userland':
            probed_args += ['-u']

        elif tstype == 'hardware':
            # Hardware timestamping is the default action and does not 
            # need to be passed to probed. However, it requires the 
            # interface name to enable timestamping for.
            try:
                ifname = self.config.get_param('interface')
            except NotFoundError:
                ifname = "eth0"
                self.logger.info("Interface not found in config. Falling back to default (%s)" % ifname)

            probed_args += ['-i', ifname]

        # config file
        probed_args += ['-f', self.probed_cfg_path]

        try:
            # \todo - Redirect to /dev/null!
            self.probed = subprocess.Popen(probed_args, 
                stdout=self.null, stderr=self.null, shell=False)
            self.logger.debug('Probe application started, pid %d', self.probed.pid)
        except Exception, e:
            self.logger.critical("Unable to start probe application (%s): %s" % (e.__class__.__name__, e))
            raise ProbedError("Unable to start probed application: (%s): %s" % (e.__class__.__name__, e))

        time.sleep(1)
        if self.probed.poll() != None:
            self.logger.error("Probed not running after 1 second. args: %s" % str(probed_args))
            raise ProbedError("Probed not running after 1 second")


    def open_fifo(self):
        try:
            self.fifo = open(self.config.get_param('fifopath'), 'r');
        except Exception, e:
            self.logger.critical("Unable to open fifo: %s" % e)


    def stop(self):
        """ Stop thread execution """
        self.thread_stop = True


    def reload(self):
        """ Reload probed application """

        self.probed.send_signal(signal.SIGHUP)


    def run(self):
        """ Function which is run when thread is started.
        
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

        self.probed.terminate()
        self.fifo.close()

class ProbedError(Exception):
    pass

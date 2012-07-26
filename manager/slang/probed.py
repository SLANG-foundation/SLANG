#! /usr/bin/python
#
# probed.py
#
# Starts up 'probed' and received measurement results from it.
#

import subprocess
import sys
import logging
import threading
import time
import resource
import signal

import probe
import config


class Probed(threading.Thread):
    """ Manages the application 'probed'.

        This class handles everything regarding the application 'probed' which
        performs all measurments and feeds us with measurement results.
    """

    probed = None
    fifo = None
    logger = None
    config = None
    pstore = None
    _flag_thread_stop = False
    _flag_run_stats = False
    nrun = 0


    def __init__(self, pstore):
        """ Constructor.

            Starts up probed and opens the FIFO used for communication.
        """

        threading.Thread.__init__(self)

        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config.Config()

        self.pstore = pstore
        self.null = open('/dev/null', 'w')

        # start probe application
        self.start_probed()

        # Try to open fifo
        while self.fifo is None:
            self.logger.debug("Waiting for FIFO...")
            time.sleep(1)
            self.open_fifo()


    def start_probed(self):
        """ Start probed application.
        """

        probed_args = ['/usr/bin/probed', '-q']
        probed_args += ['-p', self.config.get('port')]
        probed_args += ['-d', self.config.get('fifopath')]
        # timestamping type - hardware is default
        tstype = self.config.get('timestamp')
        if tstype == 'kernel':
            probed_args += ['-k']

        elif tstype == 'userland':
            probed_args += ['-u']

        elif tstype == 'hardware':
            # Hardware timestamping is the default action and does not
            # need to be passed to probed. However, it requires the
            # interface name to enable timestamping for.
            probed_args += ['-i', self.config.get('interface')]

        # config file
        probed_args += ['-f', self.config.get('probed_cfg')]

        try:
            # \todo - Redirect to /dev/null!
            self.probed = subprocess.Popen(probed_args,
                stdout=self.null, stderr=self.null, shell=False)
            self.logger.debug('Started "probed", pid %d', self.probed.pid)
        except Exception, e:
            estr = 'probed failed to start (%s): %s' % (e.__class__.__name__, e)
            self.logger.critical(estr)
            raise ProbedError("Unable to start probed: %s" % (e))

        time.sleep(1)
        if self.probed.poll() != None:
            self.logger.error("Error starting %s" % str(probed_args))
            raise ProbedError('Probed not running after 1 second')


    def open_fifo(self):
        try:
            self.fifo = open(self.config.get('fifopath'), 'r');
        except Exception, e:
            self.logger.critical('Unable to open fifo: %s' % e)


    def stop(self):
        """ Stop thread execution.

            Sets the stop-flag which will make sure the thread is stopped
            during the next iteration.
        """
        self._flag_thread_stop = True


    def run_stats(self):
        """ Log thread statistics.
        """
        self._flag_run_stats = True


    def reload(self):
        """ Reload probed application.
        """

        self.probed.send_signal(signal.SIGHUP)
        self.pstore.flush_queue()


    def run(self):
        """ Start thread.

            Will infinitely read from fifo.
        """

        while True:

            # Has anyone waved the stop flag?
            if self._flag_thread_stop:
                break

            # Has anyone waved the statistics flag?
            if self._flag_run_stats is True:
                self.logger.debug("thread %d run time: %f" %
                    (self.ident, resource.getrusage(1)[0]))
                self._flag_run_stats = False

            # check if probed is alive
            if self.probed.poll() != None:
                self.logger.warning('probed not running!')
                self.fifo.close()
                try:
                    self.start_probed()
                except Exception, e:
                  pass
                self.pstore.flush_queue()
                continue

            # Is FIFO open?
            if self.fifo.closed:
                self.logger.warn('FIFO closed. Retrying...')
                time.sleep(1)
                self.open_fifo()
                continue

            try:
                # ~780 probes can be held in fifo buff before pause
                data = self.fifo.read(28)
            except Exception, e:
                self.logger.error('Unable to read from FIFO: %s' % e)
                time.sleep(1)
                continue

            # error condition - handle in nice way!
            # \todo Handle read from dead fifo in a nice way.
            if len(data) < 1:
                continue

            # Create Probe object from data and send to ProbeStore
            try:
                p = probe.from_struct(data)
                self.pstore.add(p)
            except Exception, e:
                self.logger.error("Probe %s: %s" % (e.__class__.__name__, e))

            self.nrun += 1


        # Main loop ended. Shut down!
        self.probed.terminate()
        self.fifo.close()


class ProbedError(Exception):
    """ Exception for errors related to probed.
    """
    pass

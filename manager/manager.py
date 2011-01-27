#! /usr/bin/python

import sys
import os
import logging
import threading
from signal import *
from SimpleXMLRPCServer import SimpleXMLRPCServer, SimpleXMLRPCRequestHandler

import config
import probestore
import probed

class Manager:

    logger = None
    config = None
    pstore = None

    def __init__(self):
        """Constructor
        """

        self.config = config.Config()
        self.logger = logging.getLogger(self.__class__.__name__)
        self.pstore = probestore.ProbeStore()
        self.probed = probed.Probed(self.pstore)

        # define XML-RPC server
        self.server = SimpleXMLRPCServer(("localhost", 8000),
            requestHandler=RequestHandler,
            logRequests=False)
        self.server.register_introspection_functions()

        # export functions
        self.server.register_function(self.reload_config)
        
    def reload_config(self):
        """ Reload configuration

           Fetches configuration from central node and saves to disk.
           Then, send a SIGHUP to the probe application to make it reload
           the configuration.
        """

        self.logger.info("Reloading configuration")

        # fetch config

        # write to disk

        # send SIGHUP
        self.probe.send_signal(SIGHUP)

        return 1;

    def run(self):
        
        self.probed.start()
        self.server.serve_forever()

class RequestHandler(SimpleXMLRPCRequestHandler):
    rpc_paths = ('/xmlrpc',)

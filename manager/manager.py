#! /usr/bin/python

import subprocess
import sys
import os
import logging
from signal import *
from SimpleXMLRPCServer import SimpleXMLRPCServer, SimpleXMLRPCRequestHandler

class Manager:

  def __init__(self, config):
    """Constructor
    """

    self.config = config
    self.logger = logging.getLogger('Manager')

    # start probe application
    try:
      self.probe = subprocess.Popen(['./sla-ng', 'c'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False)
      self.logger.debug('Probe application started')
    except :
      self.logger.critical("Unable to start probe application!")
      sys.exit(1)

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
    self.server.serve_forever()

class RequestHandler(SimpleXMLRPCRequestHandler):
  rpc_paths = ('/xmlrpc',)

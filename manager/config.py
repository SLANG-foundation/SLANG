#! /ust/bin/python

from lxml import etree
import logging

class Config:

  filename = None
  __shared_state = {}

  def __init__(self, filename=None):
    """ Constructor
        
        Creates config instance and makes sure configuration file is loaded.
    """

    self.__dict__ = self.__shared_state

    if len(self.__shared_state) == 0 and filename == None:
      raise ConfigError("No config file chosen")
    elif len(self.__shared_state) > 0:
      return self

    self.logger = logging.getLogger(self.__class__.__name__)

    self.filename = filename
    self.readFile()

  def readFile(self):
    """ Reads configuration file.
        
        Reads the configuration file passed to the config object constructor.
    """
    self.logger.debug('Loading config file', filename)
    self.tree = tree = etree.parse(filename)

  def getParam(self, param):
    """ Get a config parameter
        
        Returns a string containing the value for config patameter param.
    """
    pass

class ConfigError(Exception):
  pass

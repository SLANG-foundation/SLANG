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

        elif len(self.__shared_state) == 0:
            
            # creating a brand new instance
            self.filename = filename
            self.logger = logging.getLogger(self.__class__.__name__)
            self.logger.debug("Initializing configuration module.")
            self.read_file()

    def read_file(self):
        """ Reads configuration file.
                
                Reads the configuration file passed to the config object constructor.
        """
        self.logger.info('Loading config file %s' % self.filename)
        self.tree = etree.parse(self.filename)

    def get_param(self, param):
        """ Get a config parameter
                
                Returns a string containing the value for config patameter param.
        """
        r = self.tree.xpath(param)
        if len(r) == 0:
            raise NotFoundError

        return r[0].text

class ConfigError(Exception):
    pass

class NotFoundError(ConfigError):
    pass

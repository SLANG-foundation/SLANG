#! /ust/bin/python

import logging

class Config:

    filename = None
    __shared_state = {}
    
    def __init__(self, filename=None):
        """ Constructor
                
                Creates config instance and makes sure configuration 
                file is loaded.
        """

        self.__dict__ = self.__shared_state

        if len(self.__shared_state) == 0 and filename == None:
            raise ConfigError("No config file chosen")

        elif len(self.__shared_state) == 0:
            
            # creating a brand new instance
            self.filename = filename
            self.logger = logging.getLogger(self.__class__.__name__)
            self.logger.debug("Initializing configuration module.")

    def get_param(self, param):
        """ Get a config parameter
                
                Returns a string containing the value for config 
                patameter param.
        """
        f = open(self.filename, 'r')
        lines = f.readlines()
        if len(lines) < 5:
            raise ConfigError("Invalid configuration file")
        if param == 'host':
            return lines[0].strip()
        if param == 'secret':
            return lines[1].strip()
        if param == 'port':
            return lines[2].strip()
        if param == 'timestamp':
            return lines[3].strip()
        if param == 'interface':
            return lines[4].strip()
        if param == 'fifopath':
            return '/tmp/probed.fifo' 
        if param == 'dbpath':
            return '/tmp/probed.db' 
        raise ConfigError("Invalid config parameter")

    def get_path(self):
        """ Get path to config file. """
        return self.filename

class ConfigError(Exception):
    pass

class NotFoundError(ConfigError):
    pass

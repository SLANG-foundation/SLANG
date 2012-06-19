#! /ust/bin/python

import logging

class Config:
    """ Configuration for SLA-NG manager.

        The configuration file path does only need to be passed to the
        constructor the first time in is instanciated.
    """

    filename = None
    lines = None
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
            self.logger.debug('Initializinging configuration module')
            try:
                fh = open(self.filename, 'r')
                self.lines = fh.readlines()
            except IOError, e:
                estr = "Unable to read manager config: %s" % str(e)
                self.logger.critical(estr)
                raise ConfigError(estr)

        if len(self.lines) < 5:
            raise ConfigError('Invalid configuration file')

    def get(self, param):
        """ Get a config parameter

                Returns a string containing the value for config
                patameter param.
        """
        if param == 'fifopath':
            return '/tmp/probed.fifo'
        if param == 'dbpath':
            return ':memory:'
        if param == 'rpcport':
            return '8000'
        if param == 'probed_cfg':
            return '/tmp/probed.conf'
        if param == 'configurl':
            return self.lines[0].strip()
        if param == 'secret':
            return self.lines[1].strip()
        if param == 'port':
            return self.lines[2].strip()
        if param == 'timestamp':
            return self.lines[3].strip()
        if param == 'interface':
            return self.lines[4].strip()
        raise ConfigError("Invalid config parameter")

    def get_path(self):
        """ Get path to config file. """
        return self.filename

class ConfigError(Exception):
    """ Config base exception class. """
    pass

class NotFoundError(ConfigError):
    """ Unknown config key. """
    pass

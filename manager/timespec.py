class Timespec:
    """ A nanosecond resolution timestamp. """

    self.sec = None
    self.nsec = None

    def __init__(self, sec, nsec):
        """ Create Timespec from seconds and nanoseconds. """
        self.sec = sec
        self.nsec = nsec

    def __add__(self, other):
        """ Performs addition of two Timespeces. """
        return None

    def __sub__(self, other):
        """ Performs subtraction of two Timespeces. """
        return None

    def __str__(self):
        """ Returns a string representation of the Timespec. """
        return "%d.%09d" % (self.sec, self.nsec)

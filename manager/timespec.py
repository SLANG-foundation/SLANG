class Timespec:
    """ A nanosecond resolution timestamp. """

    sec = None
    nsec = None

    def __init__(self, sec, nsec):
        """ Create Timespec from seconds and nanoseconds. """

        self.sec = sec
        self.nsec = nsec

    def __add__(self, other):
        """ Performs addition of two Timespeces. """

        res_sec = self.sec + other.sec
        res_nsec = self.nsec + other.nsec
        if (res_nsec >= 1000000000):
            res_sec += 1
            res_nsec -= 1000000000
        
        return Timespec(res_sec, res_nsec)

    def __sub__(self, other):
        """ Performs subtraction of two Timespeces. """

        res_sec = self.sec - other.sec
        res_nsec = self.nsec - other.nsec
        if res_nsec < 0:
            res_sec -= 1
            res_nsec += 1000000000

        return Timespec(res_sec, res_nsec)

    def __cmp__(self, other):
        """ Performs comparisons 

        "Should return a negative integer if self < other, zero if 
        self == other, a positive integer if self > other."

        """
        if self.sec < other.sec:
            return -1
        elif self.sec > other.sec:
            return 1
        elif self.nsec < other.nsec:
            return -1
        elif self.nsec > other.nsec:
            return 1
        else:
            return 0
        
    def __nonzero__(self):
        """ Checks if the Timespec is nonzero for truth value testing.

        "Called to implement truth value testing and the built-in 
        operation bool(); should return False or True, or their 
        integer equivalents 0 or 1."
        """
        
        if self.sec == None or self.nsec == None:
            return False
        else:
            return True

    def __str__(self):
        """ Returns a string representation of the Timespec. """
        return "%d.%09d" % (self.sec, self.nsec)
    
    def __div__(self, other):
        """ Performs division """
        if other.__class__.__name__ != 'int':
            raise Exception('We have not implemented Timespec/Timespec, LOL!')
        res_sec = self.sec / other
        res_nsec = ((self.sec % other) * 1000000000 + self.nsec) / other
        return Timespec(res_sec, res_nsec)

    def to_nanoseconds(self):
        """ Returns timespec as nanoseconds. """

        # make sure we return None if we lack values.
        if self:
            return self.sec * 1000000000 + self.nsec
        else:
            return None

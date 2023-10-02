apilevel = "2.0"
threadsafety = 0
paramstyle = "numeric"

import sys
__plpy = sys.modules['__main__'].plpy

class Error(Exception):
    pass

class Warning(Exception):
    pass

class InterfaceError(Error):
    pass

class DatabaseError(Error):
    pass

class InternalError(DatabaseError):
    pass

class OperationalError(DatabaseError):
    pass

class ProgrammingError(DatabaseError):
    pass

class IntegrityError(DatabaseError):
    pass

class DataError(DatabaseError):
    pass

class NotSupportedError(DatabaseError):
    pass

class Connection:
    def __init__(self):
        pass

def connect(**kwargs):
    return Connection()
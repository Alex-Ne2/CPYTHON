# This test covers backwards compatibility with
# previous version of Python by bouncing pickled objects through Python 3.6
# and Python 3.9 by running xpickle_worker.py.
import os
import pathlib
import pickle
import subprocess
import sys


from test import support
from test import pickletester
from test.test_pickle import PyPicklerTests

try:
    import _pickle
    has_c_implementation = True
except ModuleNotFoundError:
    has_c_implementation = False

is_windows = sys.platform.startswith('win')

# Map python version to a tuple containing the name of a corresponding valid
# Python binary to execute and its arguments.
py_executable_map = {}

def highest_proto_for_py_version(py_version):
    """Finds the highest supported pickle protocol for a given Python version.
    Args:
        py_version: a 2-tuple of the major, minor version. Eg. Python 3.7 would
                    be (3, 7)
    Returns:
        int for the highest supported pickle protocol
    """
    major = sys.version_info.major
    minor = sys.version_info.minor
    # Versions older than py 3 only supported up until protocol 2.
    if py_version < (3, 0):
        return 2
    elif py_version < (3, 4):
        return 3
    elif py_version < (3, 8):
        return 4
    elif py_version <= (major, minor):
        return 5
    else:
        # Safe option.
        return 2

def have_python_version(py_version):
    """Check whether a Python binary exists for the given py_version and has
    support. This respects your PATH.
    For Windows, it will first try to use the py launcher specified in PEP 397.
    Otherwise (and for all other platforms), it will attempt to check for
    python<py_version[0]>.<py_version[1]>.

    Eg. given a *py_version* of (3, 7), the function will attempt to try
    'py -3.7' (for Windows) first, then 'python3.7', and return
    ['py', '-3.7'] (on Windows) or ['python3.7'] on other platforms.

    Args:
        py_version: a 2-tuple of the major, minor version. Eg. python 3.7 would
                    be (3, 7)
    Returns:
        List/Tuple containing the Python binary name and its required arguments,
        or None if no valid binary names found.
    """
    python_str = ".".join(map(str, py_version))
    targets = [('py', f'-{python_str}'), (f'python{python_str}',)]
    if py_version not in py_executable_map:
        with open(os.devnull, 'w') as devnull:
            for target in targets[0 if is_windows else 1:]:
                worker = subprocess.Popen([*target, '-c','import test.support'],
                                          stdout=devnull,
                                          stderr=devnull,
                                          shell=is_windows)
                worker.communicate()
                if worker.returncode == 0:
                    py_executable_map[py_version] = target

    return py_executable_map.get(py_version, None)



class AbstractCompatTests(PyPicklerTests):
    py_version = None
    _OLD_HIGHEST_PROTOCOL = pickle.HIGHEST_PROTOCOL

    def setUp(self):
        self.assertTrue(self.py_version)
        if not have_python_version(self.py_version):
            py_version_str = ".".join(map(str, self.py_version))
            self.skipTest(f'Python {py_version_str} not available')

        # Override the default pickle protocol to match what xpickle worker
        # will be running.
        highest_protocol = highest_proto_for_py_version(self.py_version)
        pickletester.protocols = range(highest_protocol + 1)
        pickle.HIGHEST_PROTOCOL = highest_protocol

    def tearDown(self):
        # Set the highest protocol back to the default.
        pickletester.protocols = range(pickle.HIGHEST_PROTOCOL + 1)
        pickle.HIGHEST_PROTOCOL = self._OLD_HIGHEST_PROTOCOL

    def send_to_worker(self, python, obj, proto, **kwargs):
        """Bounce a pickled object through another version of Python.
        This will pickle the object, send it to a child process where it will
        be unpickled, then repickled and sent back to the parent process.
        Args:
            python: list containing the python binary to start and its arguments
            obj: object to pickle.
            proto: pickle protocol number to use.
            kwargs: other keyword arguments to pass into pickle.dumps()
        Returns:
            The pickled data received from the child process.
        """
        target = pathlib.Path(__file__).parent / 'xpickle_worker.py'
        data = super().dumps((proto, obj), proto, **kwargs)
        worker = subprocess.Popen([*python, target],
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE,
                                  # For windows bpo-17023.
                                  shell=is_windows)
        stdout, stderr = worker.communicate(data)
        if worker.returncode == 0:
            return stdout
        # if the worker fails, it will write the exception to stdout
        try:
            exception = pickle.loads(stdout)
        except (pickle.UnpicklingError, EOFError):
            raise RuntimeError(stderr)
        else:
            if isinstance(exception, Exception):
                # To allow for tests which test for errors.
                raise exception
            else:
                raise RuntimeError(stderr)


    def dumps(self, arg, proto=0, **kwargs):
        # Skip tests that require buffer_callback arguments since
        # there isn't a reliable way to marshal/pickle the callback and ensure
        # it works in a different Python version.
        if 'buffer_callback' in kwargs:
            self.skipTest('Test does not support "buffer_callback" argument.')
        python = py_executable_map[self.py_version]
        return self.send_to_worker(python, arg, proto, **kwargs)

    def loads(self, *args, **kwargs):
        return super().loads(*args, **kwargs)

    # A scaled-down version of test_bytes from pickletester, to reduce
    # the number of calls to self.dumps() and hence reduce the number of
    # child python processes forked. This allows the test to complete
    # much faster (the one from pickletester takes 3-4 minutes when running
    # under text_xpickle).
    def test_bytes(self):
        for proto in pickletester.protocols:
            for s in b'', b'xyz', b'xyz'*100:
                p = self.dumps(s, proto)
                self.assert_is_copy(s, self.loads(p))
            s = bytes(range(256))
            p = self.dumps(s, proto)
            self.assert_is_copy(s, self.loads(p))
            s = bytes([i for i in range(256) for _ in range(2)])
            p = self.dumps(s, proto)
            self.assert_is_copy(s, self.loads(p))

    # These tests are disabled because they require some special setup
    # on the worker that's hard to keep in sync.
    test_global_ext1 = None
    test_global_ext2 = None
    test_global_ext4 = None

    # Backwards compatibility was explicitly broken in r67934 to fix a bug.
    test_unicode_high_plane = None

    # These tests fail because they require classes from pickletester
    # which cannot be properly imported by the xpickle worker.
    test_c_methods = None
    test_py_methods = None
    test_nested_names = None

    test_recursive_dict_key = None
    test_recursive_nested_names = None
    test_recursive_set = None

    # Attribute lookup problems are expected, disable the test
    test_dynamic_class = None

# Base class for tests using Python 3.7 and earlier
class CompatLowerPython37(AbstractCompatTests):
    # Python versions 3.7 and earlier are incompatible with these tests:

    # This version does not support buffers
    test_in_band_buffers = None


# Base class for tests using Python 3.6 and earlier
class CompatLowerPython36(CompatLowerPython37):
    # Python versions 3.6 and earlier are incompatible with these tests:
    # This version has changes in framing using protocol 4
    test_framing_large_objects = None

    # These fail for protocol 0
    test_simple_newobj = None
    test_complex_newobj = None
    test_complex_newobj_ex = None


# Test backwards compatibility with Python 3.6.
class PicklePython36Compat(CompatLowerPython36):
    py_version = (3, 6)

# Test backwards compatibility with Python 3.7.
class PicklePython37Compat(CompatLowerPython37):
    py_version = (3, 7)

# Test backwards compatibility with Python 3.8.
class PicklePython38Compat(AbstractCompatTests):
    py_version = (3, 8)

# Test backwards compatibility with Python 3.9.
class PicklePython39Compat(AbstractCompatTests):
    py_version = (3, 9)


if has_c_implementation:
    class CPicklePython36Compat(PicklePython36Compat):
        pickler = pickle._Pickler
        unpickler = pickle._Unpickler

    class CPicklePython37Compat(PicklePython37Compat):
        pickler = pickle._Pickler
        unpickler = pickle._Unpickler

    class CPicklePython38Compat(PicklePython38Compat):
        pickler = pickle._Pickler
        unpickler = pickle._Unpickler

    class CPicklePython39Compat(PicklePython39Compat):
        pickler = pickle._Pickler
        unpickler = pickle._Unpickler

def test_main():
    support.requires('xpickle')
    tests = [PicklePython36Compat,
             PicklePython37Compat, PicklePython38Compat, 
             PicklePython39Compat]
    if has_c_implementation:
        tests.extend([CPicklePython36Compat,
                      CPicklePython37Compat, CPicklePython38Compat, 
                      CPicklePython39Compat])
    support.run_unittest(*tests)


if __name__ == '__main__':
        test_main()

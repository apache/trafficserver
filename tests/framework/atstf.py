import os
import atexit
import signal
import json
import subprocess
import threading
import select
import fcntl
import logging
import shutil
import socket
import time
import unittest
import re
import fileinput

class ProcessManager:
    def __init__(self, **kwargs):
        """Initialize..

        :param kwargs:
        root_path -- path to the ats project dir.  ats_path, origin_path and default_config_path are inferred from this
        ats_path -- path to the traffic_sever binary
        config_path --path to the ProcessManager's JSON config file.  Defaults to 'config.json' in cwd
        origin_path -- only if origin proc type is used: path to the configurable origin script (origin.py)
        default_config_path -- path to the *.config.default default config files and default body_factory files
        max_start_sec -- max time in seconds to wait for all interfaces in the config file to come up before erroring
        log_level - log level threshold from the python logging module (e.g,. logging.INFO)
        """

        self.__processes = {}  # process_name => same object as returned by process.Popen
        self.__lock = threading.Lock()  # protects self.__processes
        self.__is_running = False
        self.__ats_path = None # path to proxy/traffic_server
        self.__origin_path = None # path to tests/framework/origin.py
        self.__default_config_path = None # path to proxy/config
        self.__max_start_sec = 5

        if kwargs.has_key('root_path'):
            root_path = kwargs['root_path']
            self.__ats_path = os.path.abspath(root_path + '/proxy/traffic_server')
            self.__origin_path = os.path.abspath(root_path + '/tests/framework/origin.py')
            self.__default_config_path = os.path.abspath(root_path + '/proxy/config/')

        if kwargs.has_key('ats_path'):
            self.__ats_path = os.path.abspath(kwargs['ats_path'])

        if kwargs.has_key('origin_path'):
            self.__origin_path = os.path.abspath(kwargs['origin_path'])

        if kwargs.has_key('default_config_path'):
            self.__default_config_path = os.path.abspath(kwargs['default_config_path'])

        if not self.__ats_path:
            raise Exception("Test Manager must be initialized with 'root_path' or 'ats_path' keyword argument")

        if kwargs.has_key('max_start_sec'):
            self.__max_start_sec = kwargs['max_start_sec']

        if kwargs.has_key('config_path'):
            self.__config_path = os.path.abspath(os.getcwd() + '/' + kwargs['config_path'])
        elif os.path.isfile('config.json'):
            self.__config_path = os.path.abspath(os.getcwd() + '/config.json')
        else:
            raise Exception("Test Manager must be initialized with 'config_path' keyword argument")

        self.__conf = self.__validate_conf(json.load(open(self.__config_path, 'r'), encoding='utf-8'))

        self.__logger = logging.getLogger()

        if kwargs.has_key('log_level'):
            # If the caller has already configured a handler, use it.   Else add a console handler.
            self.__logger.setLevel(kwargs['log_level'])
            handlers = self.__logger.handlers
            if not handlers:
                handler = logging.StreamHandler()
                handler.setFormatter(logging.Formatter("%(levelname)s %(asctime)-15s - %(message)s"))
                self.__logger.addHandler(handler)

        # Any keyword arg that ends in a '.config' is a config file overlay

        # proxy/config/ssl_multicert.config.default

        atexit.register(self.__shutdown)


    def start(self):
        """ Start all processes defined in the configuration file and wait for them to start """

        signal.signal(signal.SIGTERM, self.__signal_handler)
        signal.signal(signal.SIGINT, self.__signal_handler)

        if not self.__conf['processes']:
            return

        #
        # This consists of two threads.  This thread will walk the config file and determine all interfaces
        # that will be opened by child processes.   This thread will return once it can connect to all of them.
        #
        # This thread also creates a background thread.   The background thread is responsible for actually
        # starting the child processes, relaying their stdout/stderr to the logging callback (the main reason why we
        # need a background thread), and alerting if any child process dies prematurely.
        #

        # Spawn the background thread

        self.__background_thread = threading.Thread(target=self.__background_thread)
        self.__background_thread.setDaemon(True)
        self.__is_running = True
        self.__background_thread.start()

        # Consider a service 'started' once we can connect to all of its hostnames and ports

        self.__wait_for_interfaces()

    def stop(self):
        """ Stop all processes spawned by the start function, do not wait for them to exit """
        self.__shutdown()

    def __signal_handler(self, signum, frame):
        #sys.exit(0)
        os._exit(0)

    def __shutdown(self):
        if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
            self.__logger.debug("Shutting down")

        self.__is_running = False

        try:
            self.__background_thread.join()
        except:
            # Can happen if the background thread is already dead
            pass

        with self.__lock:
            for process_name, process in self.__processes.iteritems():
                if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
                    self.__logger.debug("Killing process '%s', pid=%d", process_name, process.pid)
                process.kill()
            self.__processes = {}

    def __validate_conf(self, conf):
        """ Fixup conf file or throw exception if something in the conf file cannot be fixed up.
        :param conf: The input conf file
        :return: The fixed up conf file
        """

        if not conf.has_key('processes'):
            conf['processes'] = {}
            return conf

        for process_name, process_conf in conf['processes'].iteritems():
            if not process_conf.has_key('type'):
                raise Exception("Process '%s' is missing 'type'" % process_name)

            type = process_conf['type']

            if not process_conf.has_key('interfaces'):
                process_conf['interfaces'] = {}

            interfaces = process_conf['interfaces']

            # Don't validate config for items that have been disabled

            if process_conf.has_key('spawn') and not process_conf['spawn']:
                continue

            # Validate interfaces

            for interface_name, interface_conf in interfaces.iteritems():
                if not interface_conf.has_key('hostname'):
                    raise Exception("Interface '%s:%s' is missing 'hostname'" % (process_name, interface_name))

                if not interface_conf.has_key('port'):
                    raise Exception("Interface '%s:%s' is missing 'port'" % (process_name, interface_name))

            # Do type-specific validation

            if 'ats' == type:
                self.__validate_ats_conf(process_name, process_conf)
            elif 'python' == type:
                self.__validate_python_conf(process_name, process_conf)
            elif 'origin' == type:
                self.__validate_origin_conf(process_name, process_conf)
            elif 'other' == type:
                self.__validate_other_conf(process_name, process_conf)
            else:
                raise Exception("Process '%s' has unknown type '%s" % (process_name, type))

        return conf

    def __background_thread(self):
        try:
            for process_name, process_conf in self.__conf['processes'].iteritems():
                type = process_conf['type']

                if process_conf.has_key('spawn') and not process_conf['spawn']:
                    continue

                # Do type-specific validation

                if 'ats' == type:
                    process = self.__start_ats(process_name, process_conf)
                elif 'python' == type:
                    process = self.__start_python(process_name, process_conf)
                elif 'origin' == type:
                    process = self.__start_origin(process_name, process_conf)
                elif 'other' == type:
                    process = self.__start_other(process_name, process_conf)
                else:
                    # defensive, should be caught by validation code
                    raise Exception("Process '%s' has unknown type '%s" % (process_name, type))

                if process:
                    with self.__lock:
                        self.__processes[process_name] = process

            # monitor children and relay their output

            poll = select.poll()
            process_fds = {}  # map of fd to process_name

            with self.__lock:
                for process_name, process in self.__processes.iteritems():
                    stdout_fd = process.stdout.fileno()
                    stderr_fd = process.stderr.fileno()

                    # non-blocking io
                    flags = fcntl.fcntl(stdout_fd, fcntl.F_GETFL, 0)
                    fcntl.fcntl(stdout_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
                    flags = fcntl.fcntl(stderr_fd, fcntl.F_GETFL, 0)
                    fcntl.fcntl(stderr_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

                    # remember mapping of fd back to process name so we can later find the process object
                    process_fds[stdout_fd] = process_name
                    process_fds[stderr_fd] = process_name

                    # always interested in read, error, and close events
                    poll.register(stdout_fd, select.POLLIN | select.POLLERR | select.POLLHUP | select.POLLPRI)
                    poll.register(stderr_fd, select.POLLIN | select.POLLERR | select.POLLHUP | select.POLLPRI)

            while self.__is_running:
                events = poll.poll(100) # 100 milliseconds

                if not self.__is_running:
                    break

                if not events:
                    continue

                for fd, flag in events:
                    if not process_fds.has_key(fd):
                        # already removed, late poll event
                        continue

                    process_name = process_fds[fd]

                    with self.__lock:
                        if not self.__processes.has_key(process_name):
                            # already removed by shutdown event
                            return
                        process = self.__processes[process_name]

                    if flag & (select.POLLIN | select.POLLPRI):
                        buffer = os.read(fd, 32 * 1024)

                        if not buffer:
                            if self.__logger and self.__logger.isEnabledFor(logging.ERROR):
                                self.__logger.error("%s: process closed pipe prematurely, killing", process_name)
                            self.__cleanup_process(process_name, process, poll, process_fds)

                        buffer = buffer.rstrip()

                        if buffer:
                            if fd == process.stderr.fileno():
                                if self.__logger and self.__logger.isEnabledFor(logging.WARNING):
                                    self.__logger.warn("%s: %s" % (process_name, buffer))
                            elif fd == process.stdout.fileno():
                                if self.__logger and self.__logger.isEnabledFor(logging.INFO):
                                    self.__logger.info("%s: %s" % (process_name, buffer))

                    elif flag & select.POLLERR:
                        if self.__logger and self.__logger.isEnabledFor(logging.ERROR):
                            self.__logger.error("%s: error reading from process pipe, killing", process_name)
                        self.__cleanup_process(process_name, process, poll, process_fds)

                    elif flag & select.POLLHUP:
                        if self.__logger and self.__logger.isEnabledFor(logging.ERROR):
                            self.__logger.error("%s: process closed pipe prematurely, killing", process_name)
                        self.__cleanup_process(process_name, process, poll, process_fds)
        except:
            self.__shutdown()
            raise


    def __cleanup_process(self, process_name, process, poll, process_fds):
        process.kill()

        with self.__lock:
            self.__processes.pop(process_name, None)

        poll.unregister(process.stderr.fileno())
        poll.unregister(process.stdout.fileno())
        process_fds.pop(process.stderr.fileno(), None)
        process_fds.pop(process.stdout.fileno(), None)

    def __validate_ats_conf(self, process_name, process_conf):
        if not process_conf.has_key('interfaces'):
            raise Exception("Process '%s' must specify 1+ 'interfaces'", process_name)

        if not process_conf.has_key('root'):
            raise Exception("Process '%s' must specify a 'root'", process_name)

    def __start_ats(self, process_name, process_conf):
        if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
            self.__logger.debug("Starting ATS process '%s'", process_name)

        args = [self.__ats_path]

        if process_conf.has_key('args'):
            for arg in process_conf['args']:
                args.append(str(arg))

        interfaces = process_conf['interfaces']
        root = process_conf['root']

        # Build ats args

        args.append('--httpport')
        fd = 7

        for interface_name, interface_conf in interfaces.iteritems():
            port = interface_conf['port']

            if interface_conf.has_key('type'):
                type = interface_conf['type']
            else:
                raise Exception("Interface '%s:%s' is missing 'type'" % (process_name, interface_name))

            if 'http' == type:
                args.append("%d:fd=%d" % (port, fd));
                fd += 1
            else:
                raise Exception("Interface '%s:%s' has unknown type '%s" % (process_name, interface_name, type))

        # clean and recreate var dirs

        cwd = os.getcwd()
        ts_root = os.path.normpath(cwd + '/' + root)

        self.__generate_ts_root(process_name, process_conf, ts_root)

        # Spawn ATS.

        env = os.environ.copy()
        env['TS_ROOT'] = ts_root

        if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
            self.__logger.debug("Spawning ATS '%s' with TS_ROOT=%s", ' '.join(args), ts_root)

        return subprocess.Popen(args, shell=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=ts_root, \
                                env=env, bufsize=0)

    def __generate_ts_root(self, process_name, process_conf, ts_root):
        # Clean and recreate ts_root

        if os.path.isdir(ts_root):
            shutil.rmtree(ts_root)

        for dir in ['var/trafficserver', 'var/log/trafficserver', 'etc/trafficserver/body_factory']:
            os.makedirs(ts_root + '/' + dir)

        # Copy default config files

        if process_conf.has_key('default_config_path'):
            src_config_path = process_conf['default_config_path']
        else:
            src_config_path = self.__default_config_path

        dst_config_path = ts_root + '/etc/trafficserver/'

        for file in os.listdir(src_config_path):
            if not file.endswith('.config.default'):
                continue

            shutil.copy(src_config_path + '/' + file, dst_config_path + os.path.splitext(file)[0])

        # Copy default body factory

        default_body_factory_path = src_config_path + '/body_factory/default/'

        for file in os.listdir(default_body_factory_path):
            if file.startswith('Makefile'):
                continue

            shutil.copy(default_body_factory_path + '/' + file, dst_config_path + '/body_factory/' + file)

        # Apply transformations to the config files we just copied

        if not process_conf.has_key('config'):
            return

        config = process_conf['config']

        for file_name, transforms in config.iteritems():
            src_file = dst_config_path + file_name

            if not os.path.isfile(src_file):
                raise Exception("Cannot transform non-existent config file '%s'" % src_file)

            # Optimize data structure

            substitutions = {}
            appends = []

            for transform in transforms:
                if transform.has_key('match') and transform.has_key('substitute'):
                    substitutions[re.compile(transform['match'])] = transform['substitute']

                # if append, pull out and add to append list
                if transform.has_key('append'):
                    appends.append(transform['append'])

            # Generate new config file, then move it over

            dst_file =  src_file + '.tmp'
            fp = open(dst_file, 'w')

            try:
                os.chmod(dst_file, 0555)

                for line in fileinput.input(src_file):
                    replaced = False
                    for regex, substitution in substitutions.iteritems():
                        if regex.match(line):
                            replaced = True
                            fp.write(substitution)
                            fp.write('\n')
                            break

                    if not replaced:
                        fp.write(line)

                for append in appends:
                    fp.write(append)
                    fp.write('\n')
            finally:
                fp.close()

            shutil.move(dst_file, src_file)

    def __validate_python_conf(self, process_name, process_conf):
        if not process_conf.has_key('script'):
            raise Exception("Process '%s' must specify a 'script'", process_name)

    def __start_python(self, process_name, process_conf):
        args = ['python', process_conf['script']]

        if process_conf.has_key('args'):
            for arg in process_conf['args']:
                args.append(str(arg))

        if process_conf.has_key('cwd'):
            cwd = process_conf['cwd']
        else:
            cwd = os.getcwd()

        if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
            self.__logger.debug("Spawning Python script '%s'", " ".join(args))

        return subprocess.Popen(args, shell=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd,
                                env=os.environ, bufsize=0)

    def __validate_origin_conf(self, process_name, process_conf):
        if not self.__origin_path:
            raise Exception("Test Manager must be initialized with 'root_path' or 'origin_path' keyword argument")

        if not process_conf.has_key('interfaces'):
            raise Exception("Process '%s' must specify 1+ 'interfaces'", process_name)

        if not process_conf.has_key('actions'):
            raise Exception("Process '%s' must specify 1+ 'actions'", process_name)

    def __start_origin(self, process_name, process_conf):
        if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
            self.__logger.debug("Starting origin process '%s'", process_name)

        args = ['python', self.__origin_path, process_name, self.__config_path]

        if process_conf.has_key('cwd'):
            cwd = process_conf['cwd']
        else:
            cwd = os.getcwd()

        if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
            self.__logger.debug("Spawning origin script: %s", ' '.join(args))

        return subprocess.Popen(args, shell=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd,
                                env=os.environ, bufsize=0)


    def __validate_other_conf(self, process_name, process_conf):
        if not process_conf.has_key('executable'):
            raise Exception("Process '%s' must specify an 'executable'", process_name)

    def __start_other(self, process_name, process_conf):
        args = [process_conf['executable']]

        if process_conf.has_key('args'):
            for arg in process_conf['args']:
                args.append(str(arg))

        if process_conf.has_key('cwd'):
            cwd = process_conf['cwd']
        else:
            cwd = os.getcwd()

        if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
            self.__logger.debug("Starting other process '%s'", ' '.join(args))

        return subprocess.Popen(args, shell=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd,
                                env=os.environ, bufsize=0)

    def __wait_for_interfaces(self):
        """ Block until we can connect to all interfaces specified in the conf file """

        # Walk the config file and start all processes, remembering the hostnames and
        # ports of all their interfaces in the process

        hostports = []

        for process_name, process_conf in self.__conf['processes'].iteritems():
            type = process_conf['type']
            interfaces = process_conf['interfaces']

            if process_conf.has_key('spawn') and not process_conf['spawn']:
                # Arguable, don't check interface if we are not responsible for starting the process
                continue

            for interface_name, interface_conf in interfaces.iteritems():
                hostports.append({'process_name': process_name, 'hostname': interface_conf['hostname'],
                                  'port': interface_conf['port']})


        # poll.

        connect_timeout_sec = 1
        poll_sleep_sec = 1

        timeout_abs_sec = time.time() + self.__max_start_sec

        # Have to check and bail if self.__is_running is false in case the background thread failed to start procs

        while timeout_abs_sec > time.time() and self.__is_running:
            for hostport in hostports:
                process_name = hostport['process_name']
                hostname = hostport['hostname']
                port = hostport['port']

                if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
                    self.__logger.debug("Checking '%s' interface '%s:%d'", process_name, hostname, port)

                # This supports IPv6

                try:
                    s = socket.create_connection((hostname, port), timeout=connect_timeout_sec)
                    s.close()
                    hostports.remove(hostport)

                    if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
                        self.__logger.debug("'%s' interface '%s:%d' is up", process_name, hostname, port)
                except:
                    pass

            if not hostports:
                break

            time.sleep(poll_sleep_sec)

        if not self.__is_running:
            raise Exception("Aborted before all procs could start")

        if hostports:
            process_names = []
            for hostport in hostports:
                process_names.append(hostport['process_name'])
            raise Exception("Timeout waiting for the following processes to start: %s" % ', '.join(process_names))

        if self.__logger and self.__logger.isEnabledFor(logging.DEBUG):
            self.__logger.debug("All interfaces are up")

def ___load_suite(test_cases):
    suite = unittest.TestSuite()
    loader = unittest.TestLoader()
    ids = set()

    for test_case in test_cases:
        suite.addTest(loader.loadTestsFromTestCase(test_case))

        for name in loader.getTestCaseNames(test_case):
            ids.add("%s.%s.%s" % (test_case.__module__, test_case.__name__, name))

    return (suite, ids)

def __report_list(file, list, type, message, ids):
    for result in list:
        test = result[0]
        description = result[1]
        id = test.id().split('.')
        classname = '.'.join(id[:len(id) - 1])
        name = id[len(id) - 1]

        file.write('\t<testcase classname="%s" name="%s" time="%d">\n' % (classname, name, 0))
        file.write('\t\t<%s message="%s">\n<![CDATA[%s]]>\n\t\t</%s>\n' % (type, message, description, type))
        file.write('\t</testcase>\n')

        ids.remove(test.id()) # The only thing left will be non-errors and non-failures


def ___report(path, name, results, ids):
    file = open(path, 'w')

    file.write('<testsuite name="%s" tests="%d" errors="%d" failures="%d" skip="0">\n' % \
               (name, results.testsRun, len(results.errors), len(results.failures)))

    __report_list(file, results.failures, 'failure', 'failed assertion', ids)
    __report_list(file, results.errors, 'error', 'exception', ids)

    for id in ids:
        idp = id.split('.')
        classname = '.'.join(idp[:len(idp) - 1])
        name = idp[len(idp) - 1]
        file.write('\t<testcase classname="%s" name="%s" time="%d"/>\n' % (classname, name, 0))

    file.write('</testsuite>')
    file.close()

def run_tests(**kwargs):
    if not kwargs.has_key('test_cases'):
        raise Exception("'test_cases' kwarg must be supplied")

    test_cases = kwargs['test_cases']

    if kwargs.has_key('name'):
        name = kwargs['name']
    else:
        name = 'ats_test_suite'

    if kwargs.has_key('report'):
        report = kwargs['report']
    else:
        report = 'report.xml'

    runner = unittest.TextTestRunner()
    suite, ids = ___load_suite(test_cases)
    results = runner.run(suite)

    ___report(report, name, results, ids)

    return results.wasSuccessful()

def parse_config(config_path='config.json', process_name=None):
    """ Get the test manager's parsed configuration file
    :param process_name: Optional: if supplied return only subdoc for the process.  Else return the entire config
    """

    if not os.path.isfile(config_path):
        raise Exception("Config file '%s' does not exist" % config_path)

    conf = json.load(open(config_path, 'r'), encoding='utf-8')

    if not process_name:
        return conf

    if not conf.has_key('processes'):
        raise Exception("Configuration has no 'processes' section")

    if not conf['processes'].has_key(process_name):
        raise Exception("Configuration has no '%s' process" % process_name)

    return conf['processes'][process_name]



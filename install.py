#!/usr/bin/env python

from subprocess import call
import os, sys

remote = "root@sqtest"

def do(cmd):
    return call(cmd.split())

def reboot():
    """
    Reboot remote system.
    """
    cmd = "ssh %s reboot" % remote
    return do(cmd)

def copy(filename):
    """
    Copy kernel at <filename> to remote
    """
    cmd = "scp %s %s:~/" % (filename, remote)
    return do(cmd)

def install(filename):
    """
    Install the kernel at remote (package must already be over there)
    """
    _, remote_filename = os.path.split(filename)
    cmd = "ssh %s dpkg -i %s" % (remote, remote_filename)
    return do(cmd)

def copy_and_install(filename):
    """
    Copy the kernel at <filename> over to remote and install at remote
    """
    if not copy(filename):
        return install(filename)
    else:
        return True

def launch(filename):
    """
    Copies the module over, installs it, and reboots the remote
    """
    if not copy(filename):
        return reboot()
    else:
        return True

def usage_exit():
    print "Usage: %s {install|reboot|launch}" % sys.argv[0]
    sys.exit(2)

def main():
    if len(sys.argv) == 1:
        usage_exit()
    status = 0
    if sys.argv[1] == 'install':
        if len(sys.argv) != 3:
            usage_exit()
        status = copy(sys.argv[2])
    if sys.argv[1] == 'launch':
        if len(sys.argv) != 3:
            usage_exit()
        status = launch(sys.argv[2])
    if sys.argv[1] == 'reboot':
        if len(sys.argv) != 2:
            usage_exit()
        status = reboot()
    return status 

if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""Run SQL (stdin) against the `website` DB; credentials read server-side from dbcgen.conf."""
import configparser
import os
import subprocess
import sys

c = configparser.ConfigParser()
c.read(os.path.expanduser('~/wow/tools/dbcgen/dbcgen.conf'))
m = c['mysql']
env = dict(os.environ, MYSQL_PWD=m['password'].strip())
args = ['mysql', '-h', m.get('host', '127.0.0.1').strip(), '-u', m['user'].strip(), 'website'] + sys.argv[1:]
sys.exit(subprocess.run(args, stdin=sys.stdin, env=env).returncode)

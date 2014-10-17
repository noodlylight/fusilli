#!/usr/bin/env python2
# -*- coding: UTF-8 -*-

#Helper script used during development

# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, 
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
# Authors: Michael Bitches (noodlylight@gmail.com)
# Copyright (C) 2014 Michael Bitches

import os
import argparse

parser = argparse.ArgumentParser(description="FSM Launcher")
parser.add_argument("--prefix",
                    help="prefix")

args = parser.parse_args()

if args.prefix != None:
    f = open (os.path.join ("fsm.in"), "rt")
    data = f.read ()
    f.close ()

    data = data.replace ("path_to_python_interpreter", "/usr/bin/python2")
    data = data.replace ("@installprefix@", args.prefix)
    data = data.replace ("@version@", "1.2.3.4")

    f = open (os.path.join ("fsm"), "wt")
    f.write (data)
    f.close ()

    os.system("python2 fsm")
else:
    print "No prefix given"
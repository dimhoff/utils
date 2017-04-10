#!/usr/bin/env python
# p1_reader.py - Electricity meter P1 port data dumper
#
# Copyright (c) 2017, David Imhoff <dimhoff.devel@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the author nor the names of its contributors may
#       be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
# EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
import sys
from optparse import OptionParser
import crcmod.predefined
import re
from threading import Timer
import time
import serial
import json

SERIAL_DEVICE = '/dev/ttyUSB0'

# Interval between logging energy usage
LOG_INTERVAL = ( 5 * 60 )
LOG_FILENAME = '/dev/shm/wattcher.log'
STATUS_FILENAME = '/dev/shm/p1_status.json'

STATE_IDLE=0
STATE_ACTIVE=1

REGEX_GAS = re.compile('^\([^)]*\)\((\d+.\d+)\*m3\)$')
REGEX_ELEC = re.compile('^\((\d+.\d+)\*kWh\)$')

last_reading = ""

def process_p1_telegram(values):
    """Callback called for every successfully received telegram

    :param values: Telegram entries
    :type values: Dictionary with OBIS reference strings as keys
    """
    global last_reading

    # Process gas reading
    gas_match = REGEX_GAS.match(values['0-1:24.2.1'])
    if gas_match:
        gas_value = int(float(gas_match.group(1)) * 1000)
    else:
        gas_value = ''

    # Process electricity reading
    elec_high_match = REGEX_ELEC.match(values['1-0:1.8.2'])
    elec_low_match = REGEX_ELEC.match(values['1-0:1.8.1'])
    if elec_high_match and elec_low_match:
        elec_value = int(float(elec_high_match.group(1))*1000) + int(float(elec_low_match.group(1))*1000)
    else:
        elec_value = ''

    # Set electricity and gas values
    last_reading = "{};{}".format(elec_value, gas_value)

    # Split P1 lists into arrays
    try:
        for idx in values:
            v = values[idx]
            if len(v) < 1:
                continue
            if v[0] == '(' and v[-1] == ')':
                # Split array
                v = v[1:-1].split(")(")

                if len(v) == 1:
                    v = v[0]

                values[idx] = v
    except:
        values["error"] = "Unable to split arrays"

    # Dump all entries to JSON file
    with open(STATUS_FILENAME, 'w') as outf:
        json.dump(values, outf)


last_log_time = int(time.time()) / LOG_INTERVAL
ser = None
state = STATE_IDLE
while True:
    try:
        if ser is None:
            ser = serial.Serial(SERIAL_DEVICE, 115200, timeout=1,
                    xonxoff=False, rtscts=False, dsrdtr=False)

        line = ser.readline()
    except serial.SerialException:
        if ser is not None:
            ser.close()
        ser = None

        line = None
        time.sleep(1)

    if not line:
        # Print last reading every 5 minutes, starting at a full hour(eg. 8:05,
        # 8:10, etc.).
        # Note: Since whole timing is based on amount of lines in file it is
        # rather important that this print is actually done, even if serial
        # port doesn't work.
        if int(time.time()) / LOG_INTERVAL != last_log_time:
            with open(LOG_FILENAME, 'a') as outf:
                outf.write("{}\n".format(last_reading))
            last_log_time = int(time.time()) / LOG_INTERVAL
        continue

    # Parse P1 telegram into an dictionary and call process_p1_telegram() once
    # the telegram is successfully received
    if line.startswith('/'):
        state = STATE_ACTIVE
        values = {}
        crc = crcmod.predefined.Crc('crc-16')
        crc.update(line)
    elif state == STATE_ACTIVE:
        if line.startswith('!'):
            state = STATE_IDLE
            crc.update('!')
            try:
                received_crc = int(line[1:], 16)
            except:
                received_crc = -1
            if crc.crcValue == received_crc:
                try:
                    process_p1_telegram(values)
                except:
                    pass
        else:
            crc.update(line)
            id_end = line.find('(')
            if id_end != -1:
                values[line[0:id_end]] = line[id_end:].rstrip()

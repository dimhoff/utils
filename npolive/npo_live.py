#!/usr/bin/env python
# npo_live.py - Watch Dutch public-service broadcaster without requiring Flash
#
# usage: python npo_live.py 1 > /tmp/ned1.mpg
#        mplayer /tmp/ned1.mpg
import httplib
import subprocess
import sys
import re
import time

debug=False

if sys.argv[1] == '1':
    CHANNEL='ned1'
    HOST='l2cmab590816d0005235f018000000.fbe90e69834d23d4.kpnsmoote1f.npostreaming.nl'
elif sys.argv[1] == '2':
    CHANNEL='ned2'
    HOST='l2cm40b38f8e13005235eb7e000000.bf1978eac3cb3a48.kpnsmoote2b.npostreaming.nl'
elif sys.argv[1] == '3':
    CHANNEL='ned3'
    HOST="l2cmf100d67a2c00523354ec000000.4adf34aef8c4e150.kpnsmoote1b.npostreaming.nl"
else:
    sys.stderr.write("Invalid channel number\n");
    exit(1);

last_stream_id=0
while True :
    con = httplib.HTTPConnection(HOST)
    con.request("GET", "/d/live/npo/tvlive/%s/%s.isml/%s.m3u8" % (CHANNEL, CHANNEL, CHANNEL))
    if debug:
        sys.stderr.write("Getting primary playlist: http://{0}{1}\n".format(HOST, "/d/live/npo/tvlive/%s/%s.isml/%s.m3u8" % (CHANNEL, CHANNEL, CHANNEL)))
    resp = con.getresponse()
    if resp.status != 200:
        print("bad response to m3u8")
        exit(1)

    respdata1 = resp.read()
    use_next=False
    next_url=""
    for l in respdata1.splitlines():
        if use_next:
            next_url=l.strip()
            break
        if l.find("RESOLUTION=608x342") != -1:
            use_next=True

    if next_url == "":
        print("failed to find second m3u url")
        exit(1)

    for i in range(0, 50):
        con.request("GET", "/d/live/npo/tvlive/%s/%s.isml/%s" % (CHANNEL, CHANNEL, next_url))
        if debug:
            sys.stderr.write("Getting secondary playlist: http://{0}{1}\n".format(HOST, "/d/live/npo/tvlive/%s/%s.isml/%s" % (CHANNEL, CHANNEL, next_url)))
        resp = con.getresponse()
        if resp.status != 200:
            sys.stderr.write("bad response to m3u82\n")
            exit(1)

        respdata1 = resp.read()
        stream_urls=[]
        for l in respdata1.splitlines():
            if l.startswith("#"):
                continue
            stream_urls.append(l)

        if len(stream_urls) == 0:
            sys.stderr.write("failed to find ts url\n")
            exit(1)

        for url in stream_urls:
            stream_id=int(re.match(".*-(\d+)\.ts", url).group(1))
            if stream_id <= last_stream_id:
                time.sleep(1)
                continue

            last_stream_id = stream_id
            sys.stderr.write("%d\n" % stream_id)

            get_url="/d/live/npo/tvlive/%s/%s.isml/%s" % (CHANNEL, CHANNEL, url)
            con.request("GET", get_url)
            if debug:
                sys.stderr.write("Getting data: http://{0}{1}\n".format(HOST, get_url))
            resp = con.getresponse()
            if resp.status != 200:
                sys.stderr.write("bad response(%d) to ts: %s\n" % (resp.status, get_url))
                exit(1)

            respdata = resp.read(1024)
            while respdata:
                sys.stdout.write(respdata)
                respdata = resp.read(1024)

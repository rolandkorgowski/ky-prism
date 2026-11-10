#!/usr/bin/env python3
"""Read-only CPU/thread snapshot of a user's process; never signals or kills it."""
import argparse
import json
import os
from pathlib import Path
import time


def stat(path):
    raw=path.read_text()
    end=raw.rfind(')')
    fields=raw[end+2:].split()
    return {'name':raw[raw.find('(')+1:end], 'state':fields[0],
            'ticks':int(fields[11])+int(fields[12]), 'start':int(fields[19])}


def threads(root):
    rows={}
    for task in (root/'task').iterdir():
        try:
            row=stat(task/'stat')
            row['wait_channel']=(task/'wchan').read_text().strip()
            rows[task.name]=row
        except (OSError,ValueError,IndexError):
            continue # Threads may end during collection.
    return rows


def sample(pid,seconds):
    root=Path('/proc')/str(pid)
    if root.stat().st_uid!=os.geteuid():
        raise ValueError('Uruchom jako właściciel procesu, bez sudo.')
    start=stat(root/'stat');before=threads(root);clock=time.monotonic()
    time.sleep(seconds)
    end=stat(root/'stat');after=threads(root);elapsed=time.monotonic()-clock
    if start['start']!=end['start']:raise ValueError('PID został ponownie użyty; powtórz pomiar.')
    hz=os.sysconf('SC_CLK_TCK');rows=[]
    for tid,row in after.items():
        previous=before.get(tid)
        # A new thread's ticks were all consumed during this interval.
        old=previous['ticks'] if previous and previous['start']==row['start'] else 0
        rows.append({'tid':int(tid),'name':row['name'],'state':row['state'],
                     'cpu_percent_one_core':round(max(0,row['ticks']-old)/hz/elapsed*100,2),
                     'wait_channel':row['wait_channel']})
    try: descriptors=len(list((root/'fd').iterdir()))
    except OSError:descriptors=None
    status={}
    for line in (root/'status').read_text().splitlines():
        if ':' in line:
            key,value=line.split(':',1);status[key]=value.strip()
    return {'pid':pid,'process':end['name'],'duration_seconds':round(elapsed,3),
            'cpu_percent_one_core':round((end['ticks']-start['ticks'])/hz/elapsed*100,2),
            'rss':status.get('VmRSS'),'open_descriptors':descriptors,
            'threads':sorted(rows,key=lambda row:row['cpu_percent_one_core'],reverse=True),
            'note':'400% means four fully busy CPU cores. A snapshot identifies busy threads; it does not prove a leak or the cause. No process was stopped, traced or killed.'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid',type=int,help='PID procesu ky-prism')
    parser.add_argument('--seconds',type=float,default=2)
    args=parser.parse_args()
    if args.pid<1 or not .2<=args.seconds<=10:parser.error('PID > 0; pomiar od 0,2 do 10 sekund.')
    try:print(json.dumps(sample(args.pid,args.seconds),indent=2,ensure_ascii=False))
    except (OSError,ValueError,IndexError) as error:parser.exit(1,str(error)+'\n')


if __name__=='__main__':main()

#!/usr/bin/env python3
"""Minimal ImageDisk (.IMD) reader: prints geometry summary and writes a raw sector dump (cyl/head/sector order)."""
import sys, collections
MODES={0:'500k FM',1:'300k FM',2:'250k FM',3:'500k MFM',4:'300k MFM',5:'250k MFM'}
def parse(path):
    d=open(path,'rb').read(); i=d.index(0x1a); comment=d[:i].decode('latin1'); i+=1
    tracks=[]
    while i<len(d):
        mode,cyl,head,ns,sz=d[i:i+5]; i+=5
        smap=list(d[i:i+ns]); i+=ns
        if head&0x80: i+=ns
        if head&0x40: i+=ns
        bs=128<<sz; secs={}; bad=0
        for s in smap:
            t=d[i]; i+=1
            if t==0: secs[s]=None; bad+=1; continue
            if t in (2,4,6,8): data=bytes([d[i]])*bs; i+=1
            else: data=d[i:i+bs]; i+=bs
            if t in (5,6,7,8): bad+=1
            secs[s]=data
        tracks.append(dict(mode=mode,cyl=cyl,head=head&1,bs=bs,secs=secs,bad=bad))
    return comment,tracks
if __name__=='__main__':
    c,t=parse(sys.argv[1]); print(c.strip())
    g=collections.Counter((MODES[x['mode']],x['head'],len(x['secs']),x['bs']) for x in t)
    print('tracks',len(t),'cyls',max(x['cyl'] for x in t)+1,'heads',max(x['head'] for x in t)+1)
    for k,v in g.items(): print('  ',k,'x',v)
    print('  sectors bad/unreadable:',sum(x['bad'] for x in t), ' sector ids track0:',sorted(t[0]['secs']))
    if len(sys.argv)>2:
        with open(sys.argv[2],'wb') as o:
            for x in t:
                for s in sorted(x['secs']): o.write(x['secs'][s] or b'\xe5'*x['bs'])

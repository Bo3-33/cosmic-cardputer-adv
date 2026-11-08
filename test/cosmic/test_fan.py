#!/usr/bin/env python3
"""Check that every supplied duration and carrier setting survives compilation."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[2]
records = []
for line in (root/'assets/ir/Honeywell_Fan_HYF290B.ir').read_text().splitlines():
    if ':' not in line or line.lstrip().startswith('#'): continue
    key,value = (x.strip() for x in line.split(':',1))
    if key == 'name': records.append({'name':value})
    elif records: records[-1][key] = value
header=(root/'src/honeywell_signals.h').read_text()
assert [r['name'] for r in records] == ['Power','Oscillate','Fan_Speed']
for i,r in enumerate(records):
    original=list(map(int,r['data'].split()))
    compiled=list(map(int,re.search(r'signal'+str(i)+r'\[\] = \{([^}]+)\}',header)[1].split(',')))
    assert compiled == original
    assert r['type']=='raw' and int(r['frequency'])==38000 and float(r['duty_cycle'])==0.33
    assert all(0 < duration < 32768 for duration in compiled)
    assert (len(compiled)+1)//2 <= 240
    assert sum(compiled) < 2000000
    packed=[(compiled[n], compiled[n+1] if n+1<len(compiled) else 1) for n in range(0,len(compiled),2)]
    flattened=[duration for symbol in packed for duration in symbol]
    assert flattened[:len(original)] == original
print('Honeywell Power/Oscillate/Fan Speed: all raw timings, carrier and RMT packing limits passed')

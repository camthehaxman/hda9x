#!/usr/bin/env python3

import re
import sys

moduleName = sys.argv[1]
groups = []
segments = []
symbols = []

prevLine = None
currMode = None

for line in sys.stdin.readlines():
	if line.startswith('==='):
		currMode = prevLine.split()[0]
	if currMode == 'Group':
		m = re.match(r'(\w+)\s+(\w+:\w+)\s+.*', line)
		if m:
			groups.append(m.group(2) + ' ' + m.group(1))
	elif currMode == 'Segment':
		m = re.match(r'(\w+)\s+(\w+)\s+(\w+)\s+(\w+):(\w+)\s+(\w+)', line)
		if m:
			s = {
				'name'    : m.group(1),
				'class'   : m.group(2),
				'group'   : m.group(3),
				'addrSeg' : int(m.group(4), 16),
				'addrOff' : int(m.group(5), 16),
				'size'    : int(m.group(6), 16),
			}
			segments.append(s)
	elif currMode == 'Address':
		m = re.match(r'(\w+):(\w+)(\*|\+)?\s+(\w+)', line)
		if m:
			s = {
				'addrSeg' : int(m.group(1), 16),
				'addrOff' : int(m.group(2), 16),
				'name'    : m.group(4),
			}
			symbols.append(s)
	prevLine = line

# Sort symbols
symbols = sorted(symbols, key=lambda x: (x['addrSeg'] << 16) | x['addrOff'])

print(' ' + moduleName)
print()

print(' Start         Length Name Class')
for s in segments:
	print(' %04X:%08X %08XH %s %s' % (s['addrSeg'], s['size'], s['addrOff'], s['name'], s['class']))

#print(' Start     Length     Name                            Class')
print(' Address     Publics by Value')
for i in range(0, len(symbols)):
	sym = symbols[i]
	currSecs = [
		sec for sec in segments
		if sec['addrSeg'] == sym['addrSeg']
			and sec['addrOff'] <= sym['addrOff'] < sec['addrOff'] + sec['size']
	]
	if len(currSecs) < 1:
		continue
	sec = currSecs[0]
	if i + 1 < len(symbols) and symbols[i + 1]['addrSeg'] == sym['addrSeg']:
		end = symbols[i + 1]['addrOff']
	else:
		end = sec['addrOff'] + sec['size']
	size = end - sym['addrOff']
	#print(' %04X:%04X %05XH     %-32s%s' % (sym['addrSeg'], sym['addrOff'], size, sym['name'], sec['class']))
	print(' %04X:%08X       %s' % (sym['addrSeg'], sym['addrOff'], sym['name']))
	sym['size'] = 1

#print(' Origin    Group') 
#for g in groups:
#	print(' ' + g)

#for s in symbols:
#	print(s)

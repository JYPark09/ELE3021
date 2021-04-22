import sys

if len(sys.argv) != 2:
  print('usage: python3 {} <stat>'.format(sys.argv[0]))
  sys.exit()

with open(sys.argv[1], 'rt') as f:
  lines = f.readlines()

stats = dict()

for t, line in enumerate(lines, 0):
  pid = int(line.split()[1].strip(','))

  if not pid in stats:
    stats[pid] = []

  stats[pid].append((t, line))

print(stats.keys())

for pid in stats:
  with open(f'stat_{pid}', 'wt') as f:
    for x, y in stats[pid]:
      f.write('{} {}'.format(x, y))


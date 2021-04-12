import sys

if len(sys.argv) != 2:
  print('usage: python3 {} <stat>'.format(sys.argv[0]))
  sys.exit()

with open(sys.argv[1], 'rt') as f:
  lines = f.readlines()

stat_3_x = []
stat_3_y = []
stat_4_x = []
stat_4_y = []

for t, line in enumerate(lines, 0):
  tokens = line.split()
  pid, level = map(int, (tokens[1].strip(','), tokens[3].strip(',')))

  x_data = stat_3_x if pid == 3 else stat_4_x
  y_data = stat_3_y if pid == 3 else stat_4_y

  x_data.append(t)
  y_data.append(level)

with open('stat_3', 'wt') as f:
  for x, y in zip(stat_3_x, stat_3_y):
    f.write('{} {}\n'.format(x, y))

with open('stat_4', 'wt') as f:
  for x, y in zip(stat_4_x, stat_4_y):
    f.write('{} {}\n'.format(x, y))


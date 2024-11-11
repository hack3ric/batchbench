#!/usr/bin/python

import csv
import numpy as np
from matplotlib import pyplot as plt
import matplotlib

data = {}

with open("result.csv") as file:
  reader = csv.reader(file)
  for row in reader:
    batch_size = int(row[0])
    key = (int(row[1]), bool(int(row[2])))
    if key not in data:
      data[key] = {}
    if batch_size not in data[key]:
      data[key][batch_size] = []
    data[key][batch_size].append(float(row[3]))

# print(data)

fig, p = plt.subplots()

p.set_xlabel("Batch size")
p.set_xscale("log", base=2)
p.set_ylabel("Latency (ns)")
p.get_xaxis().set_major_formatter(matplotlib.ticker.ScalarFormatter())
p.get_xaxis().set_minor_formatter(matplotlib.ticker.NullFormatter())

for key in data:
  chunk_size, signal_batch = key
  if chunk_size not in [4096]:
    continue
  points = []
  for batch_size in data[key]:
    points.append([batch_size, np.average(data[key][batch_size])])
  [x, y] = np.transpose(points)
  p.plot(x, y, marker="x", label=f"{chunk_size}B chunks, {"with" if signal_batch else "no"} signal batching")

p.legend(loc="upper right")
plt.show()

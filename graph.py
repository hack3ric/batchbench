#!/usr/bin/python

import csv
from matplotlib.ticker import AutoLocator, AutoMinorLocator, LogLocator, LogitLocator, MaxNLocator, MultipleLocator, NullFormatter, ScalarFormatter
import numpy as np
from matplotlib import pyplot as plt
import matplotlib

data = {}

with open("result.csv") as file:
    reader = csv.reader(file)
    for row in reader:
        batch_size = int(row[0])
        thread_count = int(row[1])
        if thread_count not in data:
            data[thread_count] = {}
        if batch_size not in data[thread_count]:
            data[thread_count][batch_size] = []
        data[thread_count][batch_size].append(float(row[2]))

# print(data)

fig, p = plt.subplots()

p.set_xlabel("Segment count")
p.set_xscale("log", base=2)
p.get_xaxis().set_major_formatter(ScalarFormatter())
p.get_xaxis().set_minor_formatter(ScalarFormatter())
# p.get_xaxis().set_minor_locator(LogLocator(base=4, subs=[0.5]))
p.set_ylabel("Latency (us)")
p.set_yscale("log", base=10)
p.get_yaxis().set_major_formatter(ScalarFormatter())
p.get_yaxis().set_minor_formatter(ScalarFormatter())
p.get_yaxis().set_minor_locator(LogLocator(base=10, subs=[0.25, 0.5, 0.75]))

for thread_count in data:
    # chunk_size, signal_batch = signal_batch
    # if chunk_size not in [1024]:
    #   continue
    points = []
    for batch_size in data[thread_count]:
        if batch_size >= thread_count:
            points.append([batch_size, np.average(data[thread_count][batch_size]) / 1000])
    [x, y] = np.transpose(points)
    p.plot(
        x, y, marker="x", label=f"{thread_count} thread{"s" if thread_count > 1 else ""}"
    )

p.legend(loc="upper left")
plt.grid()
plt.show()

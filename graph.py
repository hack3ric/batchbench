#!/usr/bin/python

import csv
from matplotlib.ticker import (
    AutoLocator,
    AutoMinorLocator,
    LogLocator,
    LogitLocator,
    MaxNLocator,
    MultipleLocator,
    NullFormatter,
    ScalarFormatter,
)
import numpy as np
from matplotlib import pyplot as plt
import matplotlib

data = {}

with open("result.csv") as file:
    reader = csv.reader(file)
    for row in reader:
        batch_size = int(row[0])
        signal_batch = bool(int(row[1]))
        if signal_batch not in data:
            data[signal_batch] = {}
        if batch_size not in data[signal_batch]:
            data[signal_batch][batch_size] = []
        value = float(row[2])
        if value < 30000:
            data[signal_batch][batch_size].append(value)

# print(data)

fig, p = plt.subplots()

p.set_xlabel("Segment count")
p.set_xscale("log", base=2)
p.get_xaxis().set_major_formatter(ScalarFormatter())
p.get_xaxis().set_minor_formatter(ScalarFormatter())
# p.get_xaxis().set_minor_locator(LogLocator(base=4, subs=[0.5]))
p.set_ylabel("Latency (us)")
# p.set_yscale("log", base=10)
# p.get_yaxis().set_major_formatter(ScalarFormatter())
# p.get_yaxis().set_minor_formatter(ScalarFormatter())
# p.get_yaxis().set_minor_locator(LogLocator(base=10, subs=[0.25, 0.5, 0.75]))

for signal_batch in data:
    # chunk_size, signal_batch = signal_batch
    # if chunk_size not in [1024]:
    #   continue
    points = []
    for batch_size in data[signal_batch]:
        points.append([batch_size, np.average(data[signal_batch][batch_size]) / 1000])
    points = sorted(points, key=lambda a: a[0])
    [x, y] = np.transpose(points)
    p.plot(x, y, marker="x", label="Two-sided")

data2 = {}

with open("result-1-st.csv") as file:
    reader = csv.reader(file)
    for row in reader:
        batch_size = int(row[0])
        if batch_size > 16:
            continue
        signal_batch = bool(int(row[1]))
        if not signal_batch:
            continue
        if signal_batch not in data2:
            data2[signal_batch] = {}
        if batch_size not in data2[signal_batch]:
            data2[signal_batch][batch_size] = []
        value = float(row[2])
        if value < 30000:
            data2[signal_batch][batch_size].append(value)

for signal_batch in data2:
    # chunk_size, signal_batch = signal_batch
    # if chunk_size not in [1024]:
    #   continue
    points = []
    for batch_size in data2[signal_batch]:
        points.append([batch_size, np.average(data2[signal_batch][batch_size]) / 1000])
    points = sorted(points, key=lambda a: a[0])
    [x, y] = np.transpose(points)
    p.plot(x, y, marker="x", label="One-sided")

p.legend(loc="upper left")
plt.grid()
plt.show()

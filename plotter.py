from sys import argv
from matplotlib import pyplot as plt

fn = argv[1]
with open(fn, "r") as f:
    values = [float(i) for i in f.read().splitlines()]

plt.plot(values)
plt.ylabel("values")
plt.show()

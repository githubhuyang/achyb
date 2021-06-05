import re
from datetime import datetime
import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
import os
import csv
import sys


# executed %v, corpus cover %v, corpus signal %v, max signal %v, crashes %v, repro %v"
log_re = r"^(\d{4}\/\d{2}\/\d{2} \d{2}:\d{2}:\d{2}) VMs (\d+), executed (\d+), corpus cover (\d+), corpus signal (\d+), max signal (\d+), crashes (\d+), repro (\d+)$"
log_re = re.compile(log_re)

timestamps = []
timestamp_strs = []
prog_counts = []
cov_counts = []
sig_counts = []
msig_counts = []
crash_counts = []
repro_counts = []

fig, cov_ax = plt.subplots()
fig.tight_layout() # otherwise the right y-label is slightly clipped
crash_ax = cov_ax.twinx() # instantiate a second axes that shares the same x-axis

color = 'tab:red'
cov_ax.set_xlabel('Number of Programs Executed')
cov_ax.set_ylabel('Coverage', color=color)
cov_line = cov_ax.plot([], [], '-', color=color)[0]
cov_ax.tick_params(axis='y', labelcolor=color)
# cov_ax.set_ylim(0,150000)
# cov_ax.set_xlim(736965.74278935,736965.74591435)

color = 'tab:blue'
crash_ax.set_ylabel('KACVs', color=color)  # we already handled the x-label with cov_ax
crash_line = crash_ax.plot([], [], '-', color=color)[0]
crash_ax.tick_params(axis='y', labelcolor=color)
# crash_ax.set_ylim(0,1)
# crash_ax.set_xlim(736965.74278935,736965.74591435)
plt.draw()

directory = sys.argv[1]
fifo = sys.argv[2]

with open(fifo) as fuzz_log:
	print("Opened log FIFO")
	while True:
		data = fuzz_log.readline()
		if len(data) == 0:
			print("Writer closed")
			break
		match = log_re.match(data)
		if match:
			timestamp_str, vms, execd, cov, sig, msig, crashes, repro = match.groups()
			timestamp = datetime.strptime(timestamp_str, "%Y/%m/%d %H:%M:%S")
			vms = int(vms)
			execd = int(execd)
			cov = int(cov)
			sig = int(sig)
			msig = int(msig)
			crashes = int(crashes)
			repro = int(repro)

			timestamps.append(timestamp)
			timestamp_strs.append(timestamp_str)
			prog_counts.append(execd)
			cov_counts.append(cov)
			sig_counts.append(sig)
			msig_counts.append(msig)
			crash_counts.append(crashes)
			repro_counts.append(repro)

			# Redo plot
			# time_axis = mpl.dates.date2num(timestamps)
			cov_line.set_data(prog_counts, cov_counts)
			crash_line.set_data(prog_counts, crash_counts)
			fig.tight_layout()  # otherwise the right y-label is slightly clipped
			cov_ax.set_xlim(prog_counts[0], prog_counts[-1])
			cov_ax.set_ylim(min(cov_counts), max(cov_counts))
			crash_ax.set_xlim(prog_counts[0], prog_counts[-1])
			crash_ax.set_ylim(min(crash_counts), max(crash_counts)*1.2)

			# Draw
			fig.canvas.draw()
			plt.savefig('./%s/fuzz_progress.png' % directory)
			with open('./%s/fuzz_progress.csv' % directory, 'w') as csv_out:
				csvw = csv.writer(csv_out)
				csvw.writerow(('Time', 'Executed', 'Coverage', 'Signal', 'Max Signal', 'Crashes', 'Repro'))
				csvw.writerows(zip(timestamp_strs, prog_counts, cov_counts, sig_counts, msig_counts, crash_counts, repro_counts))

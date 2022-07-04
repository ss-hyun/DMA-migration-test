import os
import sys
import json
import matplotlib
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


# env setting
title = '[DMA] Avg migrate time'
xlabel = "chann"
x_isnum = True
ylabel = 'us'
xlim = []
ylim = []
dlabel = ['mig_up_time', 'mig_down_time'] 
graph_type = 'bar'
name = xlabel
gname = 'migrate-time-avg'

data = pd.DataFrame()

def make_graph(tag=''):
	if graph_type == 'bar':
		ax = data.plot(x=name, y=dlabel, kind=graph_type)
	elif graph_type == 'area':
		ax = data.plot.area(x=name, y=dlabel)
	if xlim: ax.set_xlim(xlim)
	if ylim: ax.set_ylim(ylim)
	plt.title(title)
	plt.xlabel(xlabel)
	plt.ylabel(ylabel)
	lg = plt.legend(bbox_to_anchor=(1.05, 1.0), loc='upper left')
	for p in ax.patches:
		left, bottom, width, height = p.get_bbox().bounds
		ax.annotate("%.2f"%(height), (left+width/2, height*1.01), ha='center')
	plt.savefig(os.path.join(gname+tag+'.png'), bbox_extra_artists=(lg,), bbox_inches='tight')
	plt.clf()

with os.scandir('.') as entries:
	for entry in entries:
		if entry.is_dir():
			path = os.path.join(entry.name)
			d = pd.DataFrame({name: [entry.name]})
			with os.scandir(path) as files:
				for f in files:
					if '.csv' in f.name:
						fd = pd.read_csv(os.path.join(path, f.name))
						if 'time' in f.name:
							m = fd.mean()
							d.loc[:,f.name[:-4]] = m[0]

			if data.empty:	data = d
			else:	data.loc[len(data)] = d.loc[0]

if x_isnum: data = data.astype({name:'int'})
data.sort_values(by=name, axis=0, inplace=True)
data.reset_index(inplace=True, drop=True)


make_graph()

title = '[DMA] Avg migrate time (ratio)'
ylabel = 'ratio'
gname = 'migrate-time-avg-percent'
for l in dlabel:
	div = data.loc[0, l]
	for i in data.index:
		if i == name: continue
		data.loc[i, l] = data.loc[i, l] / div

make_graph()


# env setting
title = 'CPU utilization AVG : core 1-5'
xlabel = 'batch'
ylabel = 'utilization(%)'
ylim = [0, 100]
dlabel = ['user', 'system'] 
graph_type = 'bar'
name = xlabel
gname = 'cpu-utilization-1-5'
core = [ 1, 2, 3, 4, 5 ]
blank = ''

data = pd.DataFrame()


with os.scandir('.') as entries:
	for entry in entries:
		if entry.is_dir():
			path = os.path.join(entry.name)
			d = pd.DataFrame({name: [entry.name]})
			with os.scandir(path) as files:
				for f in files:
					if '.csv' in f.name:
						fd = pd.read_csv(os.path.join(path, f.name))
						if 'util' in f.name:
							s = fd.groupby(by=['core']).sum()
							s = s.loc[core,:].sum()
							d.loc[:, dlabel[0]] = (s[blank+'usr'] + s[blank+'nice']) / s[blank+'interval'] * 100
							d.loc[:, dlabel[1]] = s[blank+'sys'] / s[blank+'interval'] * 100
			if data.empty:	data = d
			else:	data.loc[len(data)] = d.loc[0]
							
		
if x_isnum: data = data.astype({name:'int'})
data.sort_values(by=name, axis=0, inplace=True)
data.reset_index(inplace=True, drop=True)


make_graph()



# env setting
title = 'CPU utilization timeline : core 1-5'
xlabel = 'ms' 
ylabel = 'cpu util(%)'
dlabel = ['user', 'system'] 
graph_type = 'area'
name = xlabel
gname = 'cpu-utilization-timeline-1-5'
core = [ 1, 2, 3, 4, 5 ]
blank = ''

data = pd.DataFrame()

def core_filter(x):
	return x['core'] in core

with os.scandir('.') as entries:
	for entry in entries:
		if entry.is_dir():
			path = os.path.join(entry.name)
			with os.scandir(path) as files:
				for f in files:
					if '.csv' in f.name:
						fd = pd.read_csv(os.path.join(path, f.name))
						if 'util' in f.name:
							data = pd.DataFrame()
							for c in core:
								c = fd.groupby('core').get_group(c)
								d = pd.DataFrame({dlabel[0]:(c[blank+'usr'] + c[blank+'nice'])/c[blank+'interval']*100/len(core)})
								d.loc[:,dlabel[1]] = c[blank+'sys'] / c[blank+'interval']*100/len(core)
								d.reset_index(inplace=True, drop=True)
								if data.empty:	data = d
								else:	data.loc[:,dlabel] = data.loc[:,dlabel]+d.loc[:,dlabel]
							data.reset_index(inplace=True, drop=False)
							data.loc[:,xlabel] = data.loc[:,'index']*100
							make_graph('_'+entry.name)




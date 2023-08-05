import sys
import os
import subprocess
import argparse

parser = argparse.ArgumentParser(description="This test checks if the bubble counts in two traces generated with the same input are the same.")
parser.add_argument('program', help='the program to test')
args = parser.parse_args()

prgname = args.program
test_cmd = './tracegenerator.sh -icache off -paddr off -t {} -mode datadep -test -- {}'.format

os.chdir("../")
subprocess.call('sysctl -w kernel.randomize_va_space=0', shell=True)

for test_name in ['test1', 'test2']:
    subprocess.call(test_cmd(test_name, os.getcwd() + "/" + prgname), shell=True)

subprocess.call('sysctl -w kernel.randomize_va_space=2', shell=True)

linecount = 0
difcount = 0
with open('test1', "r") as f, open('test2', "r") as s:
    for line, line2 in zip(f, s):
        if line.split()[0] != line2.split()[0]:
            difcount += 1
            print(f"diff at: {linecount}")
        linecount += 1

print(f"total diff: {difcount}")
print(f"total line: {linecount}")

if difcount == 0:
    print("SUCCESS")
else:
    print("FAILED")

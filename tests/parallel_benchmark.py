#!/usr/bin/env python3
"""Run an optional CPU-only parallel timing probe inside the test image."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--compiler', required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'work.toy'
    artifact = Path(directory) / 'work'
    source.write_text('''def main()->void {
        var input:int[]=range(12);
        var output:int[]=parallel map(i,x in input){
            var value:int=0;
            for(var k:int=0;k<2000000;k=k+1){value=value+(k%7);}
            yield value+x;
        };
        for(var x:int in output){print(string(x));print(" ");}
    }''')
    subprocess.run([args.compiler, '--compile', '-o', str(artifact), str(source)], check=True)
    reference = None
    records = []
    for workers in [1, 2, 4]:
        started = time.perf_counter()
        result = subprocess.run([str(artifact)], env=dict(os.environ, LAB_THREADS=str(workers)), capture_output=True, check=True, timeout=60)
        elapsed = time.perf_counter() - started
        if reference is None:
            reference = result.stdout
        if result.stdout != reference:
            raise RuntimeError('parallel results differ')
        records.append({'threads': workers, 'seconds': round(elapsed, 4)})
    print(json.dumps({'timings': records, 'identical_output': True}))

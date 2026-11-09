#!/usr/bin/env python3
"""Inspect optimized IR and compare observable O0/O1 behavior in the image."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--compiler', required=True)
args, rest = parser.parse_known_args()
compiler = str(Path(args.compiler).resolve())

class OptimizerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='optimizer test ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root/'input.toy'

    def ir(self, source, level=1):
        self.source.write_text(source)
        result = subprocess.run([compiler,'--emit-llvm',f'-O{level}',str(self.source)],capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr.decode())
        return result.stdout.decode()

    def compare(self, source, stdin=b'', threads='4'):
        self.source.write_text(source)
        results=[]
        for level in [0,1]:
            artifact=self.root/f'program{level}'
            result=subprocess.run([compiler,'--compile',f'-O{level}','-o',str(artifact),str(self.source)],capture_output=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stderr.decode())
            result=subprocess.run([str(artifact)],input=stdin,capture_output=True,env=dict(os.environ,LAB_THREADS=threads),timeout=20)
            results.append((result.returncode,result.stdout,result.stderr))
        self.assertEqual(results[0],results[1])
        return results[1]

    def test_propagation_branch_pruning_and_dce(self):
        source="""def main()->int {
            var x:int=2+3;var unused:int=7*9;
            if(x==5){return x*4;}else{print("unreachable");return 1;}
        }"""
        original=self.ir(source,0)
        optimized=self.ir(source)
        self.assertIn('call i64 @lab_int_op',original)
        main=optimized.split('define i64 @user_main',1)[1].split('}',1)[0]
        self.assertIn('ret i64 20',main)
        for dead in ['call ', 'br ', 'alloca ', 'load ', 'store ']:
            self.assertNotIn(dead,main)
        self.assertEqual(self.compare(source)[0],20)

    def test_errors_and_effects_survive(self):
        for expression in ['1/0','9223372036854775807+1','-9223372036854775808/-1','-(-9223372036854775808)']:
            with self.subTest(expression=expression):
                result=self.compare('def main()->void{var unused:int='+expression+';}')
                self.assertEqual(result[0],1)
        source='def side()->int{print("side");return 4;}def main()->void{var unused:int=side();read_char();}'
        self.assertEqual(self.compare(source,b'A')[1],b'side')

    def test_loops_joins_short_circuit_and_parallel(self):
        source="""def main()->void {
            var n:int=read_char();var value:int=0;
            if(n==65){value=3;}else{value=7;}
            for(var i:int=0;i<4;i=i+1){value=value+i;}
            if(false && 1/0==0){print("bad");}
            var a:int[]=range(4);
            var b:int[]=parallel map(i,x in a){var k:int=2+3;yield x*k+value;};
            for(var x:int in b){print(string(x));print(" ");}
        }"""
        self.assertEqual(self.compare(source,b'A')[1],b'9 14 19 24 ')
        self.assertEqual(self.compare(source,b'B')[1],b'13 18 23 28 ')

    def test_semantics_before_pruning(self):
        self.source.write_text('def main()->void{if(false){var x:int=true;}}')
        result=subprocess.run([compiler,'--compile','-O1','-o',str(self.root/'bad'),str(self.source)],capture_output=True)
        self.assertEqual(result.returncode,1)
        self.assertIn(b'type error',result.stderr)

    def test_optimizer_failure(self):
        self.source.write_text('def main()->void{}')
        artifact=self.root/'bad'
        artifact.write_bytes(b'old')
        result=subprocess.run([compiler,'--compile','-O1','-o',str(artifact),str(self.source)],capture_output=True,env=dict(os.environ,LAB_OPT='/missing/opt'))
        self.assertEqual(result.returncode,2)
        self.assertFalse(artifact.exists())

if __name__=='__main__':
    unittest.main(argv=[__file__,*rest])

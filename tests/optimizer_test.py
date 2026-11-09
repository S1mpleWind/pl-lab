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

    def test_cross_block_join_and_phi(self):
        same = """def main()->int{var x:int;
            if(read_char()==65){x=4;}else{x=4;}return x+3;}"""
        ir=self.ir(same)
        main=ir.split('define i64 @user_main',1)[1].split('}',1)[0]
        self.assertIn('ret i64 7',main)
        self.assertNotIn('call i64 @lab_int_op',main)
        self.assertEqual(self.compare(same,b'A')[0],7)
        self.assertEqual(self.compare(same,b'Z')[0],7)
        dynamic="""def main()->void{
            var b:bool=read_char()==65 && read_char()==66;
            print(string(b));print(string(read_char()));}"""
        self.assertEqual(self.compare(dynamic,b'ABZ')[1],b'true90')
        self.assertEqual(self.compare(dynamic,b'XZ')[1],b'false90')

    def test_effect_analysis_and_nontermination(self):
        source='def pure()->int{return 7;}def main()->void{pure();}'
        main=self.ir(source).split('define i64 @user_main',1)[1].split('}',1)[0]
        self.assertNotIn('@user_pure',main)
        io='def leaf()->int{print("kept");return 1;}def wrap()->int{return leaf();}def main()->void{wrap();}'
        self.assertEqual(self.compare(io)[1],b'kept')
        loop='def spin()->void{while(true){}}def main()->void{spin();}'
        main=self.ir(loop).split('define i64 @user_main',1)[1].split('}',1)[0]
        self.assertIn('@user_spin',main)
        recursion='def spin()->void;def spin()->void{spin();}def main()->void{spin();}'
        main=self.ir(recursion).split('define i64 @user_main',1)[1].split('}',1)[0]
        self.assertIn('@user_spin',main)

    def test_cfg_export(self):
        import re
        self.source.write_text('def main()->int{var x:int=2+3;if(x==5){return 20;}else{return 1;}}')
        graphs=[]
        for level in [0,1]:
            result=subprocess.run([compiler,'--emit-cfg',f'-O{level}',str(self.source)],capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr.decode())
            graph=result.stdout.decode()
            self.assertTrue(graph.startswith('digraph module {'))
            nodes=set(re.findall(r'^    "([^"]+)" \[label=',graph,re.M))
            for left,right in re.findall(r'"([^"]+)" -> "([^"]+)"',graph):
                self.assertIn(left,nodes)
                self.assertIn(right,nodes)
            graphs.append(graph)
        self.assertIn('label="true"',graphs[0])
        self.assertIn('ret i64 20',graphs[1])
        self.assertLess(graphs[1].count(' -> '),graphs[0].count(' -> '))

    def test_only_llvm_verification_is_used(self):
        import shutil
        import sys
        wrapper=self.root/'verify-wrapper'
        log=self.root/'calls'
        real_opt=shutil.which('opt')
        wrapper.write_text('#!'+sys.executable+'\nimport sys,subprocess\n'
            +'from pathlib import Path\n'
            +'assert sys.argv[1:3]==["-passes=verify","-disable-output"]\n'
            +'with Path('+repr(str(log))+').open("a") as stream: stream.write("verify\\n")\n'
            +'sys.exit(subprocess.call(['+repr(real_opt)+']+sys.argv[1:]))\n')
        wrapper.chmod(0o755)
        self.source.write_text('def main()->int{return 2+3;}')
        result=subprocess.run([compiler,'--emit-llvm','-O1',str(self.source)],capture_output=True,
                              env=dict(os.environ,LAB_OPT=str(wrapper)))
        self.assertEqual(result.returncode,0,result.stderr.decode())
        self.assertEqual(log.read_text().splitlines(),['verify','verify'])

if __name__=='__main__':
    unittest.main(argv=[__file__,*rest])

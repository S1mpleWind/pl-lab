#!/usr/bin/env python3
"""Native compiler regressions; execute inside the course image."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--compiler', required=True)
args, remaining = parser.parse_known_args()
COMPILER = str(Path(args.compiler).resolve())


class CompilerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='lab compiler ')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / 'source.toy'
        self.output = self.root / 'program'

    def compile(self, source, extra=None):
        self.source.write_text(source)
        paths = [str(self.source)]
        if extra:
            second = self.root / 'second.toy'
            second.write_text(extra)
            paths.append(str(second))
        return subprocess.run([COMPILER, '--compile', '-o', str(self.output), *paths], capture_output=True, timeout=30)

    def run_program(self, source, expected=b'', stdin=b'', threads='1', extra=None):
        built = self.compile(source, extra)
        self.assertEqual(built.returncode, 0, built.stderr.decode())
        env = dict(os.environ, LAB_THREADS=threads)
        result = subprocess.run([str(self.output)], input=stdin, capture_output=True, env=env, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(result.stdout, expected)
        self.assertEqual(result.stderr, b'')
        return result

    def reject(self, source, diagnostic):
        self.source.write_text(source)
        result = subprocess.run([COMPILER, '--check', str(self.source)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1, result.stderr.decode())
        self.assertIn(diagnostic.encode(), result.stderr)
        self.assertIn(b'source.toy:', result.stderr)

    def runtime_error(self, body, diagnostic, threads='1'):
        result = self.compile('def main() -> void {' + body + '}')
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        result = subprocess.run([str(self.output)], capture_output=True, env=dict(os.environ, LAB_THREADS=threads), timeout=20)
        self.assertEqual(result.returncode, 1)
        self.assertIn(diagnostic.encode(), result.stderr)
        return result

    def test_recursion_multifile_and_order(self):
        self.run_program('def f(n:int)->int; def main() -> int { print(string(f(6))); return 0; }', b'720',
                         extra='def f(n: int) -> int {if(n<2){return 1;}return n*f(n-1);}')

    def test_int64_arithmetic_and_bool(self):
        self.run_program('''def main() -> void {
            print(string(-9223372036854775808)); print(" ");
            print(string(9223372036854775807)); print(" ");
            print(string(-7 / 3)); print(" "); print(string(-7 % 3));
            print(string(!false && 4>=3 && 3!=2 && 1<=2 && 2>1));
        }''', b'-9223372036854775808 9223372036854775807 -2 -1true')

    def test_short_circuit(self):
        self.run_program('''def bad() -> bool {print("bad"); return 1/0==0;}
            def main()->void {print(string(false&&bad())); print(string(true||bad()));
            print(string((true && (false || true)) || bad()));}''', b'falsetruetrue')

    def test_order_and_value_semantics(self):
        self.run_program('''struct P { a: int[]; }
            def mark(n:int)->int {print(string(n));return n;}
            def sum(a:int,b:int)->int{return a+b;}
            def modify(a:int[])->int[]{a[0]=9;return a;}
            def main()->void {
              print(string(sum(mark(1),mark(2))));
              var a:int[]=[4,5];var b:int[]=a;b[0]=7;
              var p:P=P(a);var q:P=p;q.a[1]=8;
              var c:int[]=modify(a);
              print(string(a[0]));print(string(p.a[1]));print(string(c[0]));
            }''', b'123459')

    def test_scopes_and_control_flow(self):
        self.run_program('''def main()->void {
            var a:int; if(true){a=3;}else{a=4;} print(string(a));
            {var a:int=8;{var a:int=9;print(string(a));}print(string(a));}
            var i:int=0;while(i<2){print(string(i));i=i+1;}
            for(var k:int=0;k<2;k=k+1){print(string(k));}
            for(var k:int in range(5,0,-2)){print(string(k));}
        }''', b'3980101531')

    def test_strings_and_input(self):
        self.run_program('''def main()->void {
            var s:string=concat("a\\0", "bc");print(s);print(substr(s,1,3));
            print(substr(s,4,4));print(string(len(s)));
            print(string(s=="a\\0bc"));print(string(s!="a"));
            print(string(int("-42")));print(string(read_char()));print(string(read_char()));
        }''', b'a\0bc\0b4truetrue-4265-1', stdin=b'A')

    def test_defaults_empty_and_struct_return(self):
        self.run_program('''struct P { n:int; s:string; b:bool; a:int[]; }
            def make()->P{return P(2,"ok",true,[]);}
            def main()->void {
              var a:P[]=new P[2];a[0].n=7;print(string(a[1].n));
              print(a[1].s);print(string(a[1].b));print(string(len(a[1].a)));
              var p:P=make();print(p.s);var empty:int[]=[];print(string(len(empty)));
            }''', b'0false0ok0')

    def test_parallel_captures_and_order(self):
        source = '''struct P { n:int; }
            def square(x:int)->int{return x*x;}
            def local(a:int[])->int {a[0]=99;return a[0];}
            def main()->void {
              var bias:int=10;var a:int[]=range(24);var saved:int[]=[8];
              var b:P[]=parallel map(i,x in a){
                var delay:int=0;for(var k:int=0;k<(24-x)*30;k=k+1){delay=delay+1;}
                var scratch:int[]=saved;scratch[0]=x;
                yield P(square(x)+i+bias+local(saved));
              };
              for(var p:P in b){print(string(p.n));print(" ");}
              print(string(saved[0]));
            }'''
        expected = (''.join(f'{x*x+x+109} ' for x in range(24)) + '8').encode()
        for threads in ['1', '2', '4']:
            with self.subTest(threads=threads):
                self.run_program(source, expected, threads=threads)

    def test_empty_parallel(self):
        self.run_program('''def main()->void{var a:int[]=[];
            var b:string[]=parallel map(i,x in a){yield string(x);};print(string(len(b)));}''', b'0', threads='4')

    def test_parallel_error_order(self):
        for threads in ['1', '2', '4']:
            with self.subTest(threads=threads):
                self.runtime_error('''var a:int[]=range(12);
                    var b:int[]=parallel map(i,x in a){
                      var delay:int=0;for(var k:int=0;k<(12-x)*100;k=k+1){delay=delay+1;}
                      var value:int=0;if(i==2){value=1/0;}elif(i==5){value=9223372036854775807+1;}
                      yield value;
                    };''', 'division by zero', threads)

    def test_runtime_boundaries(self):
        cases = [('print(string(9223372036854775807+1));', 'integer overflow'),
                 ('print(string(-9223372036854775808 / -1));', 'integer overflow'),
                 ('print(string(-9223372036854775808 % -1));', 'integer overflow'),
                 ('print(string(-(-9223372036854775808)));', 'integer overflow'),
                 ('print(string(1/0));', 'division by zero'),
                 ('var a:int[]=[1];print(string(a[-1]));', 'out of bounds'),
                 ('var a:int[]=[];a[0]=2;', 'out of bounds'),
                 ('var a:int[]=new int[-1];', 'negative length'),
                 ('print(substr("a",0,2));', 'substring bounds'),
                 ('print(string(int("12x")));', 'invalid integer string'),
                 ('var a:int[]=range(0,4,0);', 'range step')]
        for source, diagnostic in cases:
            with self.subTest(source=source):
                self.runtime_error(source, diagnostic)

    def test_invalid_thread_config(self):
        for threads in ['0','-1','x','2x','1025','']:
            with self.subTest(threads=threads):
                self.runtime_error('var a:int[]=[];var b:int[]=parallel map(i,x in a){yield x;};', 'LAB_THREADS', threads)

    def test_types_and_names(self):
        bodies = [('var a:bool=1;', 'expected bool'), ('var a:int=true+1;', 'arithmetic'),
                  ('var a:int=int(true);', 'unsupported explicit conversion'),
                  ('if(1){}', 'expected bool'), ('missing();', 'unknown function'),
                  ('var a:int=2;var a:int=3;', 'duplicate local'),
                  ('var a:int[]=[true];', 'expected int'), ('print(4);', 'expected string'),
                  ('var a:void;', 'void'), ('var a:Missing;', 'unknown type'),
                  ('var a:int=9223372036854775808;', 'out of int64'),
                  ('var a:int[]=[];var b:int=a[true];', 'expected int'),
                  ('var a:int=1;a[0]=2;', 'indexed assignment'),
                  ('var a:int=1; a();', 'not callable'), ('[1][0]=3;', 'assignment target'),
                  ('var a:int[]=new int[1];var b:int[][];', 'syntax error')]
        for body, diagnostic in bodies:
            with self.subTest(body=body):
                self.reject('def main()->void {'+body+'}', diagnostic)

    def test_initialization_and_returns(self):
        for body in ['var a:int;print(string(a));',
                     'var a:int;if(true){a=1;}print(string(a));',
                     'var a:int;while(false){a=1;}print(string(a));',
                     'var a:int;for(;false;){a=1;}print(string(a));',
                     'var a:int=a;', 'var a:int[];a[0]=1;']:
            with self.subTest(body=body):
                self.reject('def main()->void {'+body+'}', 'uninitialized' if 'int=a' not in body else 'undeclared')
        self.reject('def main()->int {if(true){return 0;}}', 'not all paths return')
        self.reject('def main()->int {return true;}', 'expected int')
        self.run_program('''def f(b:bool)->int{var a:int;if(b){return 1;}else{a=2;}return a;}
            def main()->void{print(string(f(false)));}''', b'2')

    def test_struct_errors(self):
        cases = [('struct A{a:int;a:bool;} def main()->void{}', 'duplicate struct field'),
                 ('struct A{a:A;} def main()->void{}', 'recursive struct'),
                 ('struct A{a:int;} def main()->void{var a:A=A();}', 'wrong number'),
                 ('struct A{a:int;} def main()->void{var a:A=A(1);print(string(a.b));}', 'unknown field'),
                 ('def f()->void{} def f()->void{} def main()->void{}', 'duplicate'),
                 ('def f()->void{}', 'requires def main'),
                 ('def main(a:int)->void{}', 'main must')]
        for source, message in cases:
            with self.subTest(source=source):
                self.reject(source, message)

    def test_parallel_rejections(self):
        bodies = [('print("x");yield x;', 'I/O'),
                  ('a[0]=1;yield x;', 'read-only'),
                  ('x=1;yield x;', 'read-only'),
                  ('i=2;yield x;', 'read-only'),
                  ('return 1;yield x;', 'return is forbidden'),
                  ('var b:int[]=parallel map(j,y in a){yield y;};yield x;', 'nested parallel')]
        for body, message in bodies:
            with self.subTest(body=body):
                self.reject('def main()->void{var a:int[]=[1];var b:int[]=parallel map(i,x in a){'+body+'};}', message)
        self.reject('''def hidden()->int{print("x");return 1;}
          def indirect()->int{return hidden();}
          def main()->void{var a:int[]=[1];var b:int[]=parallel map(i,x in a){yield indirect();};}''', 'may perform I/O')
        self.reject('''def nested()->int{var a:int[]=[1];var b:int[]=parallel map(i,x in a){yield x;};return b[0];}
          def main()->void{var a:int[]=[1];var b:int[]=parallel map(i,x in a){yield nested();};}''', 'nested parallel')

    def test_emit_llvm_and_compile_script(self):
        self.source.write_text('def main()->int{return 0;}')
        result = subprocess.run([COMPILER, '--emit-llvm', str(self.source)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertIn(b'define i64 @user_main', result.stdout)
        root = Path(__file__).resolve().parents[1]
        result = subprocess.run(['bash', str(root/'compile.sh'), str(self.source), str(self.output)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(self.output.read_bytes()[:4], b'\x7fELF')
        self.source.write_text('def main()->int{return true;}')
        result = subprocess.run(['bash', str(root/'compile.sh'), str(self.source), str(self.output)], capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(self.output.exists())

    def test_toolchain_failure_and_source_aliases(self):
        self.source.write_text('def main()->void{}')
        self.output.write_bytes(b'old artifact')
        env = dict(os.environ, LAB_CLANG='/nonexistent/lab-clang')
        result = subprocess.run([COMPILER, '--compile', '-o', str(self.output), str(self.source)], env=env, capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse(self.output.exists())
        root = Path(__file__).resolve().parents[1]
        for alias in ['same', 'hard', 'soft']:
            target = self.source if alias == 'same' else self.root / alias
            if alias == 'hard':
                os.link(self.source, target)
            if alias == 'soft':
                target.symlink_to(self.source)
            result = subprocess.run(['bash', str(root/'compile.sh'), str(self.source), str(target)], capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(self.source.read_text(), 'def main()->void{}')

    def test_parallel_composite_capture_and_indirect_recursion(self):
        self.run_program('''struct P { a:int[]; }
            def recurse(n:int)->int;
            def recurse(n:int)->int{if(n<=0){return 0;}return recurse(n-1)+1;}
            def main()->void{var p:P=P([3]);var input:int[]=[1,2,3];
              var result:int[]=parallel map(i,x in input){var q:P=p;q.a[0]=x;yield q.a[0]+recurse(4);};
              for(var v:int in result){print(string(v));}print(string(p.a[0]));}''', b'5673', threads='4')
        self.reject('''struct P { a:int[]; }
            def main()->void{var p:P=P([3]);var input:int[]=[1];
              var result:int[]=parallel map(i,x in input){p.a[0]=2;yield x;};}''', 'read-only')
        self.reject('''def f(n:int)->int;def g(n:int)->int;
            def f(n:int)->int{if(n<=0){print("bad");return 0;}return g(n-1);}
            def g(n:int)->int{return f(n);}
            def main()->void{var a:int[]=[1];var b:int[]=parallel map(i,x in a){yield g(x);};}''', 'may perform I/O')

    def test_recursive_prototypes(self):
        self.reject('def f(n:int)->int{return f(n);} def main()->void{}', 'preceding prototype')
        self.reject('def f(n:int)->int{return f(n);} def f(n:int)->int; def main()->void{}', 'preceding prototype')
        self.reject('def f(n:int)->int{return g(n);} def g(n:int)->int{return f(n);} def main()->void{}', 'preceding prototype')
        self.reject('def f(n:int)->int; def f(n:int)->int{return g(n);} def g(n:int)->int{return f(n);} def main()->void{}', 'preceding prototype')
        self.run_program('''def even(n:int)->bool; def odd(value:int)->bool;
          def even(n:int)->bool{if(n==0){return true;}return odd(n-1);}
          def odd(n:int)->bool{if(n==0){return false;}return even(n-1);}
          def main()->void{print(string(even(10)));print(string(odd(10)));}''', b'truefalse')
        # Consistent repeated prototypes are allowed; parameter names may differ.
        self.run_program('''def f(x:int)->int; def f(y:int)->int;
          def f(n:int)->int{if(n==0){return 1;}return f(n-1)+1;}
          def main()->void{print(string(f(3)));}''', b'4')
        # Only functions on a recursive cycle require a prototype.
        self.run_program('def main()->void{print(string(f()));} def f()->int{return 7;}', b'7')

    def test_prototype_errors(self):
        cases = [
            ('def f(n:int)->int;def f(n:bool)->int{return 0;}def main()->void{}', 'conflicting'),
            ('def f()->int;def f()->bool{return true;}def main()->void{}', 'conflicting'),
            ('def f()->int;def f(n:int)->int{return n;}def main()->void{}', 'conflicting'),
            ('def f()->void;def main()->void{}', 'declared but not defined'),
            ('def f(n:Missing)->int;def f(n:Missing)->int{return 0;}def main()->void{}', 'unknown type'),
            ('def f(a:int,a:int)->int;def f(a:int,b:int)->int{return a;}def main()->void{}', 'duplicate parameter'),
            ('def f()->void;struct f{}def main()->void{}', 'duplicate'),
            ('struct f{}def f()->void;def main()->void{}', 'duplicate'),
        ]
        for source, diagnostic in cases:
            with self.subTest(source=source):
                self.reject(source, diagnostic)
        self.source.write_text('def f(n:int)->int;')
        result = subprocess.run([COMPILER, '--dump-ast', str(self.source)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        import json
        node = json.loads(result.stdout)['files'][0]['declarations'][0]
        self.assertIsNone(node['body'])
        # File arguments establish declaration order across the program.
        result = self.compile('def f(n:int)->int{return f(n);}',
                              extra='def f(n:int)->int;def main()->void{}')
        self.assertEqual(result.returncode, 1)
        self.assertIn(b'preceding prototype', result.stderr)

    def test_float_arithmetic_assignment_and_comparison(self):
        self.run_program('''def main()->void{
            var a:float=2;var b:float=1.5;a=a+b;
            print(string(a));print(" ");print(string(2+b*2));print(" ");
            print(string(-b));print(" ");print(string(+b));print(" ");
            print(string(a/2));print(" ");print(string(a-1));print(" ");
            print(string(1e2));print(" ");print(string(1.25e-1));
            print(string(a==3.5));print(string(a!=b));print(string(a>b));
            print(string(a>=b));print(string(b<a));print(string(b<=a));
        }''', b'3.5 5 -1.5 1.5 1.75 2.5 100 0.125truetruetruetruetruetrue')

    def test_float_values_functions_and_parallel(self):
        source = '''struct P { value:float; }
            def half(x:float)->float{return x/2;}
            def recur(x:float,n:int)->float;
            def recur(x:float,n:int)->float{if(n==0){return x;}return recur(x+0.5,n-1);}
            def main()->void{
              var a:float[]=[1,2.5];a[0]=3;
              var p:P=P(half(a[1]));p.value=2;
              var empty:float[]=new float[1];print(string(empty[0]));
              print(string(recur(p.value,2)));
              var out:float[]=parallel map(i,x in a){yield x+0.25;};
              for(var x:float in out){print(" ");print(string(x));}
              var widened:float[]=parallel map(i,x in range(2)){yield x;};
              for(var x:float in widened){print(" ");print(string(x));}
            }'''
        for threads in ['1','2','4']:
            self.run_program(source,b'03 3.25 2.75 0 1',threads=threads)

    def test_float_explicit_conversions(self):
        self.run_program('''def main()->void{
            print(string(float(3)));print(" ");print(string(int(-3.9)));
            print(" ");print(string(float("1.25e2")));
            print(" ");print(string(int(float(-9223372036854775808))));
            print(" ");print(string(-0.0));
        }''', b'3 -3 125 -9223372036854775808 -0')

    def test_float_static_errors(self):
        bodies = [
            ('var x:float=true;', 'expected float'),
            ('var x:float=true+1.0;', 'arithmetic'),
            ('var x:int=1.0;', 'expected int'),
            ('var x:bool=1.0<2;', 'expected'),
            ('var x:bool=1==1.0;', 'expected'),
            ('var x:float=1.0%2;', 'remainder'),
            ('var x:float=float(true);', 'unsupported explicit conversion'),
            ('var x:bool=bool(1.0);', 'unsupported explicit conversion'),
            ('var x:int[]=new int[1.0];', 'expected int'),
            ('var x:int[]=[1];var y:int=x[0.0];', 'expected int'),
            ('var x:float=1e999;', 'finite binary64'),
            ('var x:float=1e-999;', 'finite binary64'),
        ]
        for body,message in bodies:
            with self.subTest(body=body):
                self.reject('def main()->void{'+body+'}',message)
        self.reject('def f(x:float)->void{}def main()->void{f(1);}', 'expected float')
        self.reject('def f()->float{return 1;}def main()->void{}', 'expected float')
        self.reject('def f(x:int)->float;def f(x:float)->float{return x;}def main()->void{}', 'conflicting')

    def test_float_runtime_errors(self):
        for body,message in [
            ('print(string(1.0/0.0));','float division by zero'),
            ('print(string(1e308*10.0));','float overflow'),
            ('print(string(int(1e30)));','float to int out of range'),
            ('print(string(int(float(9223372036854775807))));','float to int out of range'),
            ('print(string(float("nan")));','invalid float string'),
            ('print(string(float("inf")));','invalid float string'),
            ('print(string(float("1.5x")));','invalid float string'),
        ]:
            with self.subTest(body=body):
                self.runtime_error(body,message)

    def test_float_frontend_json(self):
        import json
        self.source.write_text('def main()->void{var f:float=1.25e-2;}')
        result = subprocess.run([COMPILER,'--dump-ast',str(self.source)],capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr.decode())
        node=json.loads(result.stdout)['files'][0]['declarations'][0]['body']['statements'][0]['initializer']
        self.assertEqual(node['kind'],'Float')
        self.assertEqual(node['literal'],'1.25e-2')
        result=subprocess.run([COMPILER,'--dump-tokens',str(self.source)],capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr.decode())
        tokens=json.loads(result.stdout)['files'][0]['tokens']
        self.assertTrue(any(t['kind']=='float literal' and t['value']=='1.25e-2' for t in tokens))

    def test_non_ascii_input(self):
        result = self.compile('def main()->void{read_char();}')
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        result = subprocess.run([str(self.output)], input=b'\xff', capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b'non-ASCII', result.stderr)


if __name__ == '__main__':
    unittest.main(argv=[__file__, *remaining])

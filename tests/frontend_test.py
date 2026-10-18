#!/usr/bin/env python3
"""Frontend regressions. Run through tests/in-image.sh, not on the host."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

COMPILER = None


class FrontendTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def write(self, source, name="case.toy"):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(source if isinstance(source, bytes) else source.encode("utf-8"))
        return path

    def invoke(self, *args):
        return subprocess.run([str(COMPILER), *map(str, args)], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=10, cwd=self.root)

    def parse(self, source, *options, name="case.toy"):
        path = self.write(source, name)
        result = self.invoke(*options, path)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        self.assertEqual(result.stderr, b"")
        return json.loads(result.stdout)

    def function(self, body, return_type="int"):
        tree = self.parse(f"def main() -> {return_type} {{ {body} }}", "--no-locations")
        return tree["files"][0]["declarations"][0]

    def expression(self, expression):
        return self.function(f"return {expression};")["body"]["statements"][0]["value"]

    def shape(self, node):
        if node["kind"] == "Binary":
            return node["operator"], self.shape(node["left"]), self.shape(node["right"])
        if node["kind"] == "Unary":
            return node["operator"], self.shape(node["operand"])
        if node["kind"] == "Integer":
            return node["magnitude"]
        if node["kind"] == "Boolean":
            return node["value"]
        return node["kind"]

    def walk(self, value):
        if isinstance(value, dict):
            yield value
            for child in value.values():
                yield from self.walk(child)
        elif isinstance(value, list):
            for child in value:
                yield from self.walk(child)

    def test_precedence_and_associativity(self):
        cases = [
            ("1 + 2 * 3", ("+", "1", ("*", "2", "3"))),
            ("(1 + 2) * 3", ("*", ("+", "1", "2"), "3")),
            ("10 - 3 - 2", ("-", ("-", "10", "3"), "2")),
            ("24 / 3 / 2 % 3", ("%", ("/", ("/", "24", "3"), "2"), "3")),
            ("1 + 2 < 4 == true || false && !true",
             ("||", ("==", ("<", ("+", "1", "2"), "4"), True),
              ("&&", False, ("!", True)))),
            ("-+3 * 2", ("*", ("-", ("+", "3")), "2")),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                self.assertEqual(self.shape(self.expression(source)), expected)

    def test_integer_magnitudes_are_lossless(self):
        for magnitude in ("0", "00042", "9223372036854775807",
                          "9223372036854775808", "9" * 200):
            with self.subTest(magnitude=magnitude):
                self.assertEqual(self.expression(magnitude)["magnitude"], magnitude)
        value = self.expression("-9223372036854775808")
        self.assertEqual(value["operator"], "-")
        self.assertEqual(value["operand"]["magnitude"], "9223372036854775808")

    def test_string_decoding_and_embedded_nul(self):
        expr = self.expression(r'"a\n\r\t\"\\\0\x41\x7f"')
        self.assertEqual(expr["value"], 'a\n\r\t"\\\0A\x7f')
        self.assertEqual(self.expression('""')["value"], "")

    def test_function_struct_and_postfix(self):
        source = """
struct Point { x: int; y: int; };
def create(x: int, y: int) -> Point { return Point(x, y); }
def field(points: Point[], i: int) -> int {
    return create(points[i].x, 2).y;
}
"""
        tree = self.parse(source, "--no-locations")
        declarations = tree["files"][0]["declarations"]
        self.assertEqual([x["kind"] for x in declarations], ["Struct", "Function", "Function"])
        self.assertEqual([x["name"] for x in declarations[0]["fields"]], ["x", "y"])
        self.assertTrue(declarations[2]["parameters"][0]["type"]["array"])
        value = declarations[2]["body"]["statements"][0]["value"]
        self.assertEqual(value["kind"], "Member")
        self.assertEqual(value["member"], "y")
        self.assertEqual(value["base"]["arguments"][0]["base"]["kind"], "Index")

    def test_declarations_arrays_casts_and_assignments(self):
        body = """
var empty: int[] = [];
var items: int[] = [1, 2, 3];
var allocated: int[] = new int[4];
var pending: int;
var flag: bool = bool(1);
items[0] = int(flag);
;
return 0;
"""
        statements = self.function(body)["body"]["statements"]
        self.assertEqual(statements[0]["initializer"]["elements"], [])
        self.assertEqual(statements[2]["initializer"]["kind"], "NewArray")
        self.assertIsNone(statements[3]["initializer"])
        self.assertEqual(statements[4]["initializer"]["kind"], "Cast")
        self.assertEqual(statements[5]["target"]["kind"], "Index")
        self.assertEqual(statements[6]["kind"], "Empty")

    def test_control_flow_and_recursion(self):
        source = """
def factorial(n: int) -> int {
    if (n <= 1) { return 1; }
    elif (n == 2) { return 2; }
    else { return n * factorial(n - 1); }
}
def main() -> void {
    while (true) { { { var x: int = 1; } } }
    for (var i: int = 0; i < 10; i = i + 1) { factorial(i); }
    for (;;) {}
    for (var i: int in range(0, 3)) { factorial(i); }
    return;
}
"""
        declarations = self.parse(source, "--no-locations")["files"][0]["declarations"]
        branch = declarations[0]["body"]["statements"][0]
        self.assertEqual(len(branch["branches"]), 2)
        self.assertIsNotNone(branch["else_body"])
        statements = declarations[1]["body"]["statements"]
        self.assertEqual([x["kind"] for x in statements], ["While", "For", "For", "RangeFor", "Return"])
        self.assertIsNone(statements[2]["initializer"])
        self.assertIsNone(statements[2]["condition"])
        self.assertIsNone(statements[2]["step"])
        self.assertIsNone(statements[-1]["value"])

    def test_parallel_map_separates_yield_from_body(self):
        source = """
def main() -> int {
    var input: int[] = [1, 2, 3];
    var output: int[] = parallel map (i, x in input) {
        var value: int = x * x;
        value = value + i;
        yield value;
    };
    return output[0];
}
"""
        tree = self.parse(source)
        mapping = tree["files"][0]["declarations"][0]["body"]["statements"][1]["initializer"]
        self.assertEqual(mapping["kind"], "ParallelMap")
        self.assertEqual((mapping["index_name"], mapping["element_name"]), ("i", "x"))
        self.assertEqual([n["kind"] for n in mapping["body"]["statements"]],
                         ["Variable", "Assignment"])
        self.assertEqual(mapping["result"]["name"], "value")
        self.assertEqual(mapping["yield_span"]["begin"]["line"], 7)

    def test_lexer_maximal_match_and_keyword_boundaries(self):
        tree = self.parse("if iffy int integer <= < == = -> - && || // end\n",
                          "--dump-tokens")
        tokens = tree["files"][0]["tokens"]
        self.assertEqual([t["kind"] for t in tokens],
                         ["if", "identifier", "int", "identifier", "<=", "<",
                          "==", "=", "->", "-", "&&", "||", "end of file"])
        self.assertEqual(tokens[1]["value"], "iffy")
        self.assertEqual(tokens[3]["value"], "integer")

    def test_positions_comments_and_crlf(self):
        source = "// head\r\ndef f() -> int {\r\n    return 0;\r\n}\r\n"
        tree = self.parse(source)
        file = tree["files"][0]
        function = file["declarations"][0]
        self.assertEqual(function["span"]["begin"]["line"], 2)
        self.assertEqual(function["span"]["begin"]["column"], 1)
        self.assertEqual(function["span"]["begin"]["offset"], source.index("def"))
        ret = function["body"]["statements"][0]
        self.assertEqual((ret["span"]["begin"]["line"], ret["span"]["begin"]["column"]), (3, 5))
        self.assertEqual(file["span"]["end"]["offset"], len(source))
        self.assertEqual(file["span"]["end"]["line"], 5)
        commented = self.parse("/* first\n second */ def f() -> int { return 0; }")
        self.assertEqual(commented["files"][0]["declarations"][0]["span"]["begin"]["line"], 2)

    def test_string_span_includes_both_quotes(self):
        source = 'def f() -> string { return "abc"; }'
        value = self.parse(source)["files"][0]["declarations"][0]["body"]["statements"][0]["value"]
        self.assertEqual(value["span"]["begin"]["offset"], source.index('"'))
        self.assertEqual(value["span"]["end"]["offset"], source.rindex('"') + 1)

    def test_no_locations_removes_nested_spans(self):
        tree = self.parse("struct A { x: int; } def f(a: A) -> int { return a.x; }",
                          "--no-locations")
        for item in self.walk(tree):
            self.assertFalse(any(key.endswith("span") for key in item))

    def test_empty_source_and_struct(self):
        self.assertEqual(self.parse("")["files"][0]["declarations"], [])
        self.assertEqual(self.parse("struct Empty {}")["files"][0]["declarations"][0]["fields"], [])

    def test_documented_full_example(self):
        example = Path(__file__).resolve().parents[1] / "examples/frontend/full.toy"
        result = self.invoke(example)
        self.assertEqual(result.returncode, 0, result.stderr)
        tree = json.loads(result.stdout)
        self.assertEqual([node["name"] for node in tree["files"][0]["declarations"]],
                         ["Point", "factorial", "main"])

    def test_semantic_checks_are_deferred(self):
        # Parse success deliberately does not mean this is a well-typed program.
        tree = self.parse('def f() -> int { var x: bool = 1; missing(); 1 = 2; return "bad"; }')
        self.assertEqual(tree["files"][0]["declarations"][0]["name"], "f")

    def test_lexical_errors(self):
        sources = [
            b'def f() -> int { @ }',
            b'def f() -> int { return 0; }\x00',
            b'def f() -> string { return "\\q"; }',
            b'def f() -> string { return "\\x80"; }',
            b'def f() -> string { return "\\x1"; }',
            b'def f() -> string { return "line\nbreak"; }',
            b'def f() -> string { return "unfinished',
            b'def f() -> string { return "slash\\',
            b'/* unterminated',
            'def f() -> string { return "中文"; }',
        ]
        for source in sources:
            with self.subTest(source=source):
                result = self.invoke(self.write(source))
                self.assertEqual(result.returncode, 1)
                self.assertIn(b"lexical error", result.stderr)
                self.assertEqual(result.stdout, b"")

    def test_syntax_errors(self):
        sources = [
            "def f() -> int { return 1 }",
            "def f( -> int {}",
            "def f() -> int {",
            "def f() -> int { if (true) return 0; }",
            "def f() -> int { let x: int = 0; }",
            "def f() -> int { yield 0; }",
            "def f() -> int { var x: int[] = [1,]; }",
            "def f() -> int { var x: int[][]; }",
            "def f() -> int { var a: int[] = parallel map (i, x in []) {}; }",
            "def f() -> int { var a: int[] = parallel map (i, x in []) { yield 1; yield 2; }; }",
            "def f() -> int { var a: int[] = parallel map (i, x in []) { if (true) { yield 1; } yield 2; }; }",
        ]
        for source in sources:
            with self.subTest(source=source):
                result = self.invoke(self.write(source))
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(b"syntax error", result.stderr)
                self.assertEqual(result.stdout, b"")

    def test_diagnostic_location_and_filename(self):
        path = self.write("def f() -> int {\n  @\n}", "目录 with spaces.toy")
        result = self.invoke(path)
        expected = f"{path}:2:3: lexical error:".encode()
        self.assertIn(expected, result.stderr)

    def test_multifile_order_and_later_error_no_partial_output(self):
        first = self.write("def first() -> int { return 1; }", "first.toy")
        second = self.write("def second() -> int { return 2; }", "second.toy")
        result = self.invoke(second, first)
        self.assertEqual(result.returncode, 0, result.stderr)
        tree = json.loads(result.stdout)
        self.assertEqual([f["declarations"][0]["name"] for f in tree["files"]],
                         ["second", "first"])
        second.write_text("def broken(", encoding="utf-8")
        for mode in ("--dump-ast", "--dump-tokens"):
            # Token dumping is lexical only; use a lexical error for its failure case.
            if mode == "--dump-tokens":
                second.write_text("@", encoding="utf-8")
            result = self.invoke(mode, first, second)
            self.assertEqual(result.returncode, 1)
            self.assertIn(str(second).encode(), result.stderr)
            self.assertEqual(result.stdout, b"")

    def test_output_file_is_written_only_after_success(self):
        path = self.write("def f() -> int { return 0; }")
        output = self.root / "tree.json"
        result = self.invoke("-o", output, path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"")
        self.assertEqual(json.loads(output.read_text())["kind"], "Program")
        output.unlink()
        path.write_text("def broken(", encoding="utf-8")
        result = self.invoke("-o", output, path)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(output.exists())

    def test_output_cannot_overwrite_source_or_alias(self):
        path = self.write("def f() -> int { return 0; }")
        original = path.read_bytes()
        alias = self.root / "alias.toy"
        os.link(path, alias)
        for output in (path, alias):
            result = self.invoke("-o", output, path)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(path.read_bytes(), original)

    def test_cli_errors_and_dash_filename(self):
        self.assertEqual(self.invoke().returncode, 2)
        self.assertEqual(self.invoke("--unknown").returncode, 2)
        self.assertEqual(self.invoke("-o").returncode, 2)
        path = self.write("")
        self.assertEqual(self.invoke("--dump-ast", "--dump-tokens", path).returncode, 2)
        self.assertEqual(self.invoke(self.root / "missing.toy").returncode, 2)
        self.assertEqual(self.invoke("--help").returncode, 0)
        dash = self.write("def f() -> int { return 0; }", "-source.toy")
        self.assertEqual(self.invoke("--", dash.name).returncode, 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    COMPILER = args.compiler.resolve()
    unittest.main(argv=[__file__, *remaining])

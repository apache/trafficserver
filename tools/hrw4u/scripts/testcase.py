#!/usr/bin/env python3
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import sys
import argparse
from pathlib import Path
from antlr4 import *
from hrw4u.hrw4uLexer import hrw4uLexer
from hrw4u.hrw4uParser import hrw4uParser
from hrw4u.visitor import HRW4UVisitor
from hrw4u.errors import wrap_error

# ➡️ Known valid marks/groups
KNOWN_MARKS = {"hooks", "conds", "ops", "vars", "examples", "invalid"}


def parse_tree(input_text):
    stream = InputStream(input_text)
    lexer = hrw4uLexer(stream)
    tokens = CommonTokenStream(lexer)
    parser = hrw4uParser(tokens)
    tree = parser.program()
    return parser, tree


def process_file(input_path, update_ast=False, update_output=False, update_error=False, verbose=False, dry_run=False):
    base = input_path.with_suffix('')
    ast_path = base.with_suffix('.ast.txt')
    output_path = base.with_suffix('.output.txt')
    error_path = base.with_suffix('.error.txt')

    input_text = input_path.read_text()

    if input_path.name.endswith(".fail.input.txt"):
        try:
            parser, tree = parse_tree(input_text)
            visitor = HRW4UVisitor(filename=str(input_path))
            visitor.visit(tree)
            print(f"❌ Unexpected success: {input_path}")
            return False

        except Exception as e:
            error_message = str(e).strip()

            if dry_run:
                print(f"# {input_path}")
                print(f"(error)\n{error_message}\n")
                return True

            if update_error:
                error_path.write_text(error_message + "\n")
                if verbose:
                    print(f"[Error updated]  {error_path}")

            return True

    else:
        try:
            parser, tree = parse_tree(input_text)
            ast_text = tree.toStringTree(recog=parser).strip()
            output_text = "\n".join(HRW4UVisitor(filename=str(input_path)).visit(tree)).strip()

            if dry_run:
                print(f"# {input_path}")
                print(output_text)
                print()
                return True

            if update_ast:
                ast_path.write_text(ast_text + "\n")
                if verbose:
                    print(f"[AST updated]    {ast_path}")

            if update_output:
                output_path.write_text(output_text + "\n")
                if verbose:
                    print(f"[Output updated] {output_path}")

            return True

        except Exception as e:
            print(f"hrw4u error: {input_path}: {e}", file=sys.stderr)
            return False


def run_batch(group=None, update_ast=False, update_output=False, update_error=False, verbose=False, dry_run=False):
    base_dir = Path("tests/data")
    pattern = "**/*.input.txt" if group is None else f"{group}/**/*.input.txt"
    input_files = sorted(base_dir.glob(pattern))

    if not input_files:
        print(f"No test files found for pattern: {base_dir}/{pattern}")
        sys.exit(1)

    total = len(input_files)
    failed = 0

    for f in input_files:
        ok = process_file(
            f, update_ast=update_ast, update_output=update_output, update_error=update_error, verbose=verbose, dry_run=dry_run)
        if not ok:
            failed += 1

    if not dry_run:
        print(f"\n✅ Updated: {total - failed}, ❌ Failed: {failed}")
    if failed:
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="Run, update, or show ANTLR test outputs.")
    parser.add_argument("input_file", type=Path, nargs="?", help="Optional single input file")
    parser.add_argument("-g", "--group", help="Test group to run (e.g. 'hooks')")
    parser.add_argument("-m", "--mark", help="Alias for --group (e.g. hooks, conds, ops, vars, invalid)")
    parser.add_argument("--update-output", action="store_true", help="Update .output.txt")
    parser.add_argument("--update-ast", action="store_true", help="Update .ast.txt")
    parser.add_argument("--update-error", action="store_true", help="Update .error.txt for failing tests")
    parser.add_argument("--update", action="store_true", help="Update AST and output (but not errors)")
    parser.add_argument("--verbose", action="store_true", help="Show updated file paths")
    parser.add_argument("--output", action="store_true", help="Print output from each test to stdout")

    args = parser.parse_args()

    group = args.mark or args.group

    if args.mark and group not in KNOWN_MARKS:
        print(f"❌ Unknown mark: {group}. Allowed: {', '.join(sorted(KNOWN_MARKS))}")
        sys.exit(1)

    update_ast = args.update or args.update_ast
    update_output = args.update or args.update_output
    update_error = args.update_error
    dry_run = args.output

    if not (update_ast or update_output or update_error or dry_run):
        print("Use --update, --update-ast, --update-output, --update-error, or --output")
        sys.exit(1)

    if args.input_file:
        process_file(
            args.input_file,
            update_ast=update_ast,
            update_output=update_output,
            update_error=update_error,
            verbose=args.verbose,
            dry_run=dry_run)
    else:
        run_batch(
            group=group,
            update_ast=update_ast,
            update_output=update_output,
            update_error=update_error,
            verbose=args.verbose,
            dry_run=dry_run)


if __name__ == "__main__":
    main()

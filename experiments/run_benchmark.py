"""Compile the production numerical core and print the frozen-holdout sanity result."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
if not compiler:
    raise SystemExit('Install a C++17 compiler to run this experiment.')
with tempfile.TemporaryDirectory() as directory:
    binary = str(Path(directory) / 'thermal_benchmark')
    sources = ['src/node_model.cpp', 'src/discretization.cpp', 'src/node_state.cpp',
               'src/protocol.cpp', 'experiments/thermal_benchmark.cpp']
    subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Itests/native_stubs', '-Iinclude', *sources, '-o', binary], cwd=root, check=True)
    result = json.loads(subprocess.check_output([binary], text=True))
print(json.dumps(result, indent=2, allow_nan=False))

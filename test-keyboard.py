#!/usr/bin/env python3
"""Compatibility entry point; implementation lives in tools/scripts."""
import runpy
from pathlib import Path
import sys

scripts = Path(__file__).resolve().parent / 'tools/scripts'
sys.path.insert(0, str(scripts))
runpy.run_path(str(scripts / 'test-keyboard.py'), run_name='__main__')

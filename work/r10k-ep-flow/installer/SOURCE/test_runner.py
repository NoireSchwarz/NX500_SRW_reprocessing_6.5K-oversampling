#!/usr/bin/env python3
"""Compatibility test entry for the refactored durable RAW422 workflow."""
from pathlib import Path
import runpy
if __name__=='__main__':
    runpy.run_path(str(Path(__file__).with_name('test_workflow.py')),run_name='__main__')

#!/usr/bin/env vpython
# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper script to deal with Blink static globals."""

import argparse
import csv
import os
import re
import subprocess
import sys


# Assumes this script is under build/
_SCRIPT_DIR = os.path.dirname(__file__)
_SCRIPT_NAME = os.path.join(_SCRIPT_DIR, os.path.basename(__file__))
_TOP_SRC_DIR = os.path.join(_SCRIPT_DIR, '..')


# Find the locations of the raw executables, as we can't use 'shell=True' in
# later invocations due to escaping issues.
if sys.platform == 'win32':
  _git = os.path.normpath(os.path.join(subprocess.check_output(
      'git bash -c "cd / && pwd -W"', shell=True).strip(), 'bin\\git.exe'))
else:
  _git = 'git'


def EachLineOfInput(input):
  """Generator that yields each line of the provided input stream."""
  while True:
    line = input.readline()
    if line == '':
      return
    yield(line)


def EachLineOfStdout(cmd, cwd=None):
  """Generator that yields each line of stdout from the provided command."""
  proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, cwd=cwd)
  for line in EachLineOfInput(proc.stdout):
    yield(line)
  proc.communicate()
  if proc.returncode != 0:
    raise subprocess.CalledProcessError('%s returned with code %d' %
        (cmd, proc.returncode))


def EachBlinkIsolateCsvLine(csv_path, other_columns=None, allow_bad_rows=False):
  """Yields (path, line, isolate_bound) for all relevant locations in a .cc file."""
  if other_columns == None:
    other_columns = []

  with open(csv_path, 'rb') as csv_file:
    csv_reader = csv.reader(csv_file)
    seen_header = False
    file_col = 0
    line_col = 1

    ISOLATE_BOUND = 'isolatebound?'
    other_cols = {}
    for other_col in other_columns:
      other_cols[other_col.strip().lower()] = -1
    other_cols[ISOLATE_BOUND] = -1
    isolate_bound_col = -1
    max_col = 1

    csv_line_no = 0
    for row in csv_reader:
      csv_line_no += 1
      if not seen_header:
        if len(row) <= line_col:
          continue
        if row[file_col].strip().lower() == 'file' and row[line_col].strip().lower() == 'line':
          for col in xrange(0, len(row)):
            colname = row[col].strip().lower()
            if colname in other_cols:
              if other_cols[colname] != -1:
                raise Exception('Found multiple columns with header "%s" is file "%s"' % (colname, csv_path))
              other_cols[colname] = col
              max_col = max(max_col, col)

          for colname, colidx in other_cols.iteritems():
            if colidx == -1:
              raise Exception('Did not find column "%s" in file "%s"' % (colname, csv_path))

          isolate_bound_col = other_cols[ISOLATE_BOUND]
          del other_cols[ISOLATE_BOUND]

          seen_header = True

        # Continue on to the next line.
        continue

      try:
        if len(row) <= max_col:
          raise Exception('Found ill-formatted row at line %d in file "%s": %s' % (csv_line_no, csv_path, row))

        path = os.path.normpath(row[file_col].strip())
        if not os.path.exists(path) or not os.path.isfile(path):
          raise Exception('Path at line %d to file "%s" does not exist: %s' % (csv_line, csv_path, path))

        line = int(row[line_col].strip())

        isolate_bound = row[isolate_bound_col].strip().lower()
        if isolate_bound == 'yes':
          isolate_bound = True
        elif isolate_bound == 'no' or isolate_bound == 'yesmanual':
          isolate_bound = False
        else:
          raise Exception('unknown IsolateBound? value at line %d of file "%s": %s' % (csv_line_no, csv_path, row[isolate_bound_col]))

        other = {}
        for colname, colidx in other_cols.iteritems():
          value = row[colidx].strip().lower()
          other[colname] = value

        yield((path, line, isolate_bound, other))

      except:
        # Reraise unless we're absorbing bad rows.
        if not allow_bad_rows:
          raise


# The types of edits, and the files that contain them.
STATIC = 0
REF = 1
THREADSAFE = 2
GLOBAL = 3

FILE_TYPES = {
    #STATIC: 'Blink Isolates_ DEFINE_STATIC_LOCAL - DEFINE_STATIC_LOCAL.csv',
    #REF: 'Blink Isolates_ DEFINE_STATIC_LOCAL - DEFINE_STATIC_REF.csv',
    #THREADSAFE: 'Blink Isolates_ DEFINE_STATIC_LOCAL - DEFINE_THREAD_SAFE_STATIC_LOCAL.csv'}
    GLOBAL: 'Blink Isolates_ DEFINE_STATIC_LOCAL - static member_static local_globals.csv'}


def LoadAllBlinkIsolateCsvFiles(allow_bad_rows=False):
  # A map of files to the edits to be done in them. The edits are in the
  # form (line_number, type, isolate_bound, other_data).
  file_edits = {}

  for file_type, path in FILE_TYPES.iteritems():
    other_columns = None
    if file_type == GLOBAL:
      other_columns = ['Type']

    # Extract the edit data from the file.
    for row in EachBlinkIsolateCsvLine(path, other_columns=other_columns,
                                       allow_bad_rows=allow_bad_rows):
      path = row[0]
      line_number = row[1]
      isolate_bound = row[2]
      other = row[3]

      if isolate_bound:
        if path not in file_edits:
          file_edits[path] = []
        file_edits[path].append((line_number, file_type, other))

  # Sort the edits for each file.
  for path, edits in file_edits.iteritems():
    edits.sort()

  return file_edits


def FindLineToEdit(lines, line_number, pattern):
  search_order = [0, 1, -1, 2, -2, 3, -3, 4, -4]
  for offset in search_order:
    search_line_number = line_number + offset
    if search_line_number not in lines:
      continue
    line = lines[search_line_number]
    m = pattern.match(line)
    if m:
      return (search_line_number, m)
  return (None, None)


INCLUDE = re.compile('^#include (["<])(.*)[">]$')
def EnsureHeaderInFile(path, lines, header, is_header=False):
  if not is_header:
    is_header = path.endswith('.h') or path.endswith('.h.tmpl')

  def InsertInclude(lines, before, after, header):
    where = (before + after) / 2.0
    print '  ...added include before line %s' % after
    lines[where] = '#include "' + header + '"\n'
    return

  old_lineidx = -1
  cur_lineidx = -1
  first_blank_line = -1
  last_line = -1
  include_block_count = 0
  last_include = None
  lineidxs = sorted(lines.keys())
  in_include_block = False
  include_block_type = None
  want_to_insert_in_block = False
  for lineidx in lineidxs:
    old_lineidx = cur_lineidx
    cur_lineidx = lineidx

    line = lines[lineidx]

    # Track the position of the first blank line.
    if first_blank_line == -1 and line.strip() == '':
      first_blank_line = old_lineidx

    m = INCLUDE.match(line)

    # If this is already included, do nothing.
    if m and m.group(2) == header:
      return

    # Keep track of how many include blocks we've seen.
    if m:
      if not in_include_block:
        in_include_block = True
        include_block_type = m.group(1)
      elif include_block_count == 0 and include_block_type == '"' and not is_header:
        # This is the first include block, but it's length is > 1
        # so its not special.
        return EnsureHeaderInFile(path, lines, header, is_header=True)
      elif include_block_type != m.group(1):
        include_block_count += 1
        include_block_type = m.group(1)
    elif in_include_block:
      if want_to_insert_in_block:
        InsertInclude(lines, last_line, lineidx, header)
        return
      in_include_block = False
      include_block_count += 1

    if not m:
      continue

    # We're processing an include.
    include = m.group(2)

    # Don't look at the first include block in non-headers.
    if is_header or include_block_count > 0:
      if include_block_type == '"':
        want_to_insert_in_block = True
        if header < include:
          InsertInclude(lines, old_lineidx, lineidx, header)
          return

    last_include = include
    last_line = lineidx

  # We didn't find where to place the include. We need to introduce a new block.

  insertbeforeidx = last_line
  if last_line == -1:
    insertbeforeidx = first_blank_line

  if insertbeforeidx == -1:
    raise Exception('Unable to find where to insert header in file: %s' % path)

  # Find the next line.
  insertafteridx = -1
  for lineidx in lineidxs:
    if lineidx <= insertbeforeidx:
      continue
    insertafteridx = lineidx
    break

  # Insert a blank line before.
  where = (insertbeforeidx + insertafteridx) / 2.0
  lines[where] = '\n'
  insertbeforeidx = where

  # Insert the include.
  InsertInclude(lines, insertbeforeidx, insertafteridx, header)


def ApplySimpleEditToFile(path, lines, line_number, pattern, replacement):
  line_no, match = FindLineToEdit(lines, line_number, pattern)
  if not line_no:
    raise Exception('Unable to edit line %d of file "%s": %s' % (line_number, path, lines[line_number]))

  edited = match.group(1) + replacement + match.group(2) + '\n'
  lines[line_no] = edited

ISOLATE_HEADER = "third_party/blink/renderer/platform/isolate.h"
DEFINE_STATIC_LOCAL = re.compile('^(.*)DEFINE_STATIC_LOCAL\((.*)$')
DEFINE_ISOLATE_BOUND = 'DEFINE_ISOLATE_BOUND('
def ApplyStaticEditToFile(path, lines, line_number):
  ApplySimpleEditToFile(path, lines, line_number, DEFINE_STATIC_LOCAL, DEFINE_ISOLATE_BOUND)
  EnsureHeaderInFile(path, lines, ISOLATE_HEADER)


DEFINE_STATIC_REF = re.compile('^(.*)DEFINE_STATIC_REF\((.*)$')
DEFINE_ISOLATE_BOUND_REF = 'DEFINE_ISOLATE_BOUND_REF('
def ApplyRefEditToFile(path, lines, line_number):
  ApplySimpleEditToFile(path, lines, line_number, DEFINE_STATIC_REF, DEFINE_ISOLATE_BOUND_REF)
  EnsureHeaderInFile(path, lines, ISOLATE_HEADER)


DEFINE_THREAD_SAFE_STATIC_LOCAL = re.compile('^(.*)DEFINE_THREAD_SAFE_STATIC_LOCAL\((.*)$')
def ApplyThreadSafeEditToFile(path, lines, line_number):
  ApplySimpleEditToFile(path, lines, line_number, DEFINE_THREAD_SAFE_STATIC_LOCAL, DEFINE_ISOLATE_BOUND)
  EnsureHeaderInFile(path, lines, ISOLATE_HEADER)

GLOBAL_REGEX = re.compile('((?:\s*static\s)?)(.*)\*\s([a-z0-9_]*)')
def ApplyGlobalEditToFile(path, lines, line_number, type):
  line_no, match = FindLineToEdit(lines, line_number, GLOBAL_REGEX)
  if not line_no:
    raise Exception('Unable to edit line %d of file "%s": %s' % (line_number, path, lines[line_number]))
  lines[line_no] = re.sub(GLOBAL_REGEX, r'\1IsolateBoundGlobalStaticPtr<\2> \3', lines[line_no])
  if type != "definition":
    EnsureHeaderInFile(path, lines, ISOLATE_HEADER)

def ApplyEditToFile(path, lines, line_number, edit_type, params):
  if edit_type == STATIC:
    assert(not params)
    ApplyStaticEditToFile(path, lines, line_number)
  elif edit_type == REF:
    assert(not params)
    ApplyRefEditToFile(path, lines, line_number)
  elif edit_type == THREADSAFE:
    assert(not params)
    ApplyThreadSafeEditToFile(path, lines, line_number)
  elif edit_type == GLOBAL:
    assert(len(params) == 1 and 'type' in params)
    ApplyGlobalEditToFile(path, lines, line_number, params['type'])
  else:
    raise Exception('Unknown edit type: %s' % edit_type)


def WriteFile(lines, outfile):
  lineidxs = sorted(lines.keys())
  for lineidx in lineidxs:
    line = lines[lineidx]
    outfile.write(line)


def ApplyEditsToFile(path, edits, write=False, max_edits=-1):
  # Read the entire file.
  lines = {}
  with open(path, 'rb') as infile:
    line_number = 0
    for line in EachLineOfInput(infile):
      line_number += 1
      lines[line_number] = line

  print 'Patching "%s"...' % path
  edit_count = 0
  for line_number, edit_type, params in edits:
    if edit_count + 1 <= max_edits:
      ApplyEditToFile(path, lines, line_number, edit_type, params)
      edit_count += 1
  print '  ...applied %d patches' % edit_count

  if write:
    if write == sys.stdout:
      WriteFile(lines, sys.stdout)
    else:
      with open(path, 'wb') as outfile:
        WriteFile(lines, outfile)
    print '  ...wrote file'


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('-a', '--allow-bad-rows', help='Allows empty/invalid rows in input data.',
                      action='store_true', default=False)
  parser.add_argument('-w', '--write', help='Actually writes output to disk instead of dry running.',
                      action='store_true', default=False)
  parser.add_argument('-s', '--stdout', help='Dumps files to stdout instead of to disk.',
                      action='store_true', default=False)
  parser.add_argument('-n', '--number', metavar='N', type=int,
                      help='Specifies how many patches to apply.', default=-1)
  args = parser.parse_args()
  if args.stdout:
    args.write = sys.stdout

  # Load the edits from the csv files.
  file_edits = LoadAllBlinkIsolateCsvFiles(allow_bad_rows=args.allow_bad_rows)

  # Remove some no longer needed edits that required manual migration.
  #del file_edits['third_party/blink/renderer/platform/heap/process_heap.cc']

  # Apply the edits.
  edit_count = 0
  file_count = 0
  patches_left = args.number
  for path, edits in file_edits.iteritems():
    patch_count = len(edits)
    if patches_left != -1:
      patch_count = min(patches_left, patch_count)

    ApplyEditsToFile(path, edits, write=args.write,
                     max_edits=patch_count)
    edit_count += patch_count
    file_count += 1

    if patches_left != -1:
      patches_left -= patch_count
    if patches_left == 0:
      break

  print 'Applied %d edits across %d files' % (edit_count, file_count)


assert __name__ == '__main__'
main()


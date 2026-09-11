# -*- coding: utf-8 -*-

"""
Reports GitHub Actions cache usage, grouped by key prefix (first segment before "-" or "/")

 Usage: python3 .ci/gha_cache_usage.py [-m|--markdown]
   -m, --markdown output as a markdown table, if not set, output as ASCII table
 Requires: authenticated gh CLI (`gh auth login` locally or set `GH_TOKEN` env variable)
"""

import argparse
import json
import re
import subprocess


DEFAULT_REPO_NAME = 'cockatrice/cockatrice'
LIMIT = 1000 # gh's default limit is only 30 entries, which is too small
SPLIT_EXPR = r'[-/]'
# Formatting params
PREFIX_LEN = 12
COUNT_LEN = 6
SIZE_LEN = 10


parser = argparse.ArgumentParser()
parser.add_argument('-m', '--markdown', action='store_true', help='output as a markdown table', default=False)
parser.add_argument('-r', '--repo', help='repository name', default=DEFAULT_REPO_NAME)
args = parser.parse_args()
markdown = args.markdown
repo = args.repo

convert_to_gib = lambda x: x / (1024**3)

# `gh cache list --json` available fields: createdAt, id, key, lastAccessedAt, ref, sizeInBytes, version. (see https://cli.github.com/manual/gh_cache_list)
response = subprocess.run(['gh', 'cache', '-R', args.repo, 'list', '--limit', str(LIMIT), '--json', 'key,sizeInBytes'], capture_output=True)
if response.returncode != 0:
    print(f"Error: gh cache list failed (check gh auth or repo access): {response.stderr.decode('utf-8')}")
    exit(1)
json_data = json.loads(response.stdout)
# find prefixes
prefixes = set()
for item in json_data:
    prefix = re.split(SPLIT_EXPR, item["key"])[0]
    prefixes.add(prefix)

num_entries = len(json_data)
total_size = sum(item["sizeInBytes"] for item in json_data)
total_size_in_gb = convert_to_gib(total_size)

summary = []
for prefix in prefixes:
    items_with_prefix = [item for item in json_data if re.split(SPLIT_EXPR, item["key"])[0] == prefix]
    count = len(items_with_prefix)
    size = sum(item["sizeInBytes"] for item in items_with_prefix)
    size_in_gb = convert_to_gib(size)
    item = {
        'prefix': prefix,
        'count': count,
        'sizeInGib': size_in_gb
    }
    summary.append(item)

summary.sort(key=lambda x: x['sizeInGib'], reverse=True)
    
if markdown:
    print('| PREFIX | COUNT | SIZE (GiB) |')
    print(f'|{'-'*PREFIX_LEN}|{'-'*COUNT_LEN}:|{'-'*SIZE_LEN}:|')
    for item in summary:
        print(f'| {item["prefix"]} | {item["count"]} | {item["sizeInGib"]:.2f} |')
    print(f'| **Total** | {num_entries} | {total_size_in_gb:.2f} |')
else:
    print(f"{'PREFIX':{PREFIX_LEN}} {'COUNT':{COUNT_LEN}} {'SIZE (GB)':{SIZE_LEN}}")
    print(f"{'-'*PREFIX_LEN} {'-'*COUNT_LEN} {'-'*SIZE_LEN}")
    for item in summary:
        print(f"{item['prefix']:{PREFIX_LEN}} {item['count']:{COUNT_LEN}} {item['sizeInGib']:{SIZE_LEN}.2f}")
    print(f"{'Total':{PREFIX_LEN}} {num_entries:{COUNT_LEN}} {total_size_in_gb:{SIZE_LEN}.2f}")

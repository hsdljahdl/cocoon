#!/bin/bash
set -e

DIR="$(dirname "$(readlink -f "$0")")"
cd "$DIR"

pipenv run python ./gpu_attest.py

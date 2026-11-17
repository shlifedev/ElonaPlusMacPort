#!/bin/zsh
set -eu
cd -- "${0:A:h}"
python3 tools/play_game.py --language ko

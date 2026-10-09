#!/bin/bash
# Downloads the free shareware doom1.wad (v1.9) and checks it.
cd "$(dirname "$0")"
curl -sL -o doom1.wad https://raw.githubusercontent.com/Akbar30Bill/DOOM_wads/master/doom1.wad
echo "5b2e249b9c5133ec987b3ea77596381dc0d6bc1d  doom1.wad" | sha1sum -c

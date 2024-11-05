#/usr/bin/env bash

if [ "$1" = edgar ]; then
    ./build/indexer "%Y-%m-%dT%H:%M:%S" examples/edgar-scraper
else
    ./build/indexer "%Y/%m/%d %H:%M:%S" examples/slog
fi

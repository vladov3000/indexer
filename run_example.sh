#/usr/bin/env bash

if [ "$1" = edgar ]; then
    ./indexer "%Y-%m-%dT%H:%M:%S" examples/edgar-scraper
else
    ./indexer "%Y/%m/%d %H:%M:%S" examples/slog
fi

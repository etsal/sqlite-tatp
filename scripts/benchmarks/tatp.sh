#!/usr/local/bin/bash

for sf in 10000 100000 1000000; do
  printf "*** TATP (scale factor %s) ***\n" "$sf"

  printf "Loading data into SQLite3...\n"
  ./tatp_sqlite3 --load --records=$sf

  printf "Evaluating SQLite3...\n"
  for journal_mode in "DELETE" "TRUNCATE" "WAL"; do
    for cache_size in "-100000" "-200000" "-500000" "-1000000" "-2000000" "-5000000"; do
      command="./tatp_sqlite3 --run --records=$sf --journal_mode=$journal_mode --cache_size=$cache_size"
      printf "%s\n" "$command"
      for trial in {1..3}; do
        printf "%s," "$trial"
        eval "$command"
      done
    done
  done
done

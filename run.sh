#!/bin/bash

for i in {0..9}; do
  for j in {0..3}; do
    # for k in {10..13}; do
      for l in {0..10}; do
        # ./client $((1<<i)) $((1<<k)) $j
        ./client $((1<<i)) $((1<<j))
        sleep 0.5
      done
    # done
  done
done

#!/bin/bash

for i in {0..9}; do
  # for j in {0..3}; do
    # for k in {10..13}; do
      for l in {0..100}; do
        # ./client $((1<<i)) $((1<<k)) $j
        ./client $((1<<i))
        sleep 0.25
      done
    # done
  # done
done

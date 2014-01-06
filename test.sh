#!/bin/bash

for ((i = 0; i < 10; i++))
do
  rm *.log
  killall lock_server
  ./rsm_tester.pl 8 9 10 11 12 13 14 15 16 
  echo "./rsm_tester.pl 8 9 10 11 12 13 14 15 16"
done

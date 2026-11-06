#!/bin/sh
# usage: sweep.sh model.bin "temp minp rep" ...   (3 samples per question)
M=$1; shift
for cfg in "$@"; do
  set -- $cfg
  echo "=== temp $1  min-p $2  repeat-penalty $3"
  for q in "hi how are you" "what is the capital of france ?" "capital of japan" "why is the sky blue" "whats a computer" "who was einstein" "how many planets are there" "tell me a joke" "what should i eat for dinner" "do u like music" "i feel sad today" "what is gravity ?"; do
    echo "> $q"
    for s in 1 2 3; do printf "   "; NW_SEED=$s NW_TEMP=$1 NW_MINP=$2 NW_REP=$3 NW_TOPK=40 ./host/test.exe "$M" "$q" chat | tail -1; done
  done
done

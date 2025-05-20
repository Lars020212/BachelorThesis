#!/bin/bash

echo $1
# Default value for type
type="-"$1


# Print the type for debugging
echo "Type: $type"

#server
while [ -z "$h2_id" ]
do
  h2_id=$(xdotool search --name "Node: h2")
  echo "h2 wait"
  sleep 1
done
echo "h2: $h2_id"

#xdotool windowactivate --sync $h2_id type "iperf -s -i 0.2"
xdotool windowactivate --sync $h2_id type "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/xjobb.sh -q -s"
xdotool windowactivate --sync $h2_id key KP_Enter


#client
sleep 1
while [ -z "$h1_id" ]
do
  h1_id=$(xdotool search --name "Node: h1")
  echo "h1 wait"
  sleep 1
done
echo "h1: $h1_id"
#xdotool windowactivate --sync $h1_id type "iperf -i 0.2 -t 1000000 -c 10.0.1.102"
xdotool windowactivate --sync $h1_id type "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/xjobb.sh -q -c"
xdotool windowactivate --sync $h1_id key KP_Enter

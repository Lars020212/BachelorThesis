#!/bin/bash
# Användning: ./xjobb.sh -l -q -s     (lokal server, QUIC)
#             ./xjobb.sh -l -q -c     (lokal klient, QUIC)
#             ./xjobb.sh -l -t -s     (lokal server, TCP)
#             ./xjobb.sh -l -t -c     (lokal klient, TCP)

while getopts "sclqt" opt; do
  case ${opt} in
    s )
      if [[ -n $local ]]; then
        echo "Local: Server"
        if [[ -n $quic ]]; then
          echo "Protocol: QUIC"
          python3 /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/run_experiment.py \
            -s S \
            -host localhost \
            -certfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/leo-emulator-server.pem \
            -keyfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/leo-emulator-server-key.pem \
            -cafile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/cacerts.pem
        else
          echo "Protocol: TCP"
          python3 /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/run_experiment.py \
            -s S \
            -host localhost \
            -certfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/leo-emulator-server.pem \
            -keyfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/leo-emulator-server-key.pem \
            -cafile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/cacerts.pem
        fi
      fi
      
      echo "Emulator: Server: "
      if [[ -n $quic ]]; then
        echo "Protocol: QUIC"
        python3 /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/run_experiment.py \
          -s S \
          -host 10.0.1.102 \
          -certfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/leo-emulator-server.pem \
          -keyfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/leo-emulator-server-key.pem \
          -cafile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/cacerts.pem
      else
        echo "Protocol: TCP"
        python3 /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/run_experiment.py \
          -s S \
          -host 10.0.1.102 \
          -certfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/leo-emulator-server.pem \
          -keyfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/leo-emulator-server-key.pem \
          -cafile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/server/cacerts.pem
      fi
      ;;
    c )
      if [[ -n $local ]]; then
        echo "Local: Client"
        if [[ -n $quic ]]; then
          echo "Protocol: QUIC"
          python3 /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/run_experiment.py \
            -c C \
            -host localhost \
            -certfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/leo-emulator-client.pem \
            -keyfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/leo-emulator-client-key.pem \
            -cafile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/cacerts.pem
        else
          echo "Protocol: TCP"
          python3 /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/run_experiment.py \
            -c C \
            -host localhost \
            -certfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/leo-emulator-client.pem \
            -keyfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/leo-emulator-client-key.pem \
            -cafile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/cacerts.pem
        fi
      fi
        echo "Emulator: Client: "
        if [[ -n $quic ]]; then
          echo "Protocol: QUIC"
          python3 /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/run_experiment.py \
            -c C \
            -host 10.0.1.102 \
            -certfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/leo-emulator-client.pem \
            -keyfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/leo-emulator-client-key.pem \
            -cafile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/cacerts.pem
        else
          echo "Protocol: TCP"
          python3 /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/src/run/run_experiment.py \
            -c C \
            -host 10.0.1.102 \
            -certfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/leo-emulator-client.pem \
            -keyfile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/leo-emulator-client-key.pem \
            -cafile /home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/cert/leo-emulator/client/cacerts.pem
        fi
      ;;
    l )
      local=true
      ;;
    q )
      quic=true
      ;;
    t )
      tcp=true
      ;;
    \? )
      echo "Invalid option: $OPTARG" 1>&2
      ;;
  esac
done


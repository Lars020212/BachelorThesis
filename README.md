# README

## Introduction
This code was used in the test environment used for my bachelor thesis in computer science.
The uploaded files are the ones i made changes to or are neccesary for the test environent to work.
The test environment found in [https://github.com/lurr3t/QUIC_compress_bachelor_thesis/tree/main/LeoEM](https://github.com/lurr3t/QUIC_compress_bachelor_thesis/tree/main)
was used as a base for the test environment used in this thesis.

## To run the code
Follow the guides created by the respective author regarding their code, so follow LeoEM's guide to get
started with the network emulation, use lsquic's guide for the QUIC implementation and so on. 

Change the files in the respective libraries to the ones featured here, so for instance use the emulator.py
that is found here instead of the one featured in LeoEM's emulation stage.

Change ALL relevant file paths (this needs to be done in several files) to the ones used in your project, or this will not work.

Important to add the Webster file or another file that you want to use to silesia folder, this file is
the one that will be sent in the network.

When the environment is set up, to run experiments as was done in this thesis, start LeoEM emulator, if the version used
is the one that is in this repository, two xterm windows will open, first h2, and after a delay h1,
when the h2 window opens, run the bash script node_commands this will run a QUIC server in h2 and a QUIC client in h1, if all
is working correctly the different protocols used for this thesis will run for the specified file sizes and ranges, after the
run is complete the mean transfer time is logged in an excel file named output in the logs folder.

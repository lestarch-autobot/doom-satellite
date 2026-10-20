"""fsw_main.py: DoomCoprocessor entry point

DoomCoprocessor runs under the Python interpreter because its frameReader component is implemented in Python through
fprime-python. This script replaces the former C++ main: it parses the same options, sets up the topology, drives it
until SIGINT/SIGTERM, and tears it down.

Usage (from the install tree, which holds fprime_py and the Python components):
    python3 build-artifacts/<platform>/DoomCoprocessor/python/fsw_main.py [-a address] [-p port] [-u port] [-f host:port]

Supplies the fsw_main() entry point required by fprime-python-runner.
"""
import argparse
import os

import fprime_py

DEFAULT_REMOTE_ADDRESS = "192.168.11.2"
DEFAULT_REMOTE_PORT = 50556
DEFAULT_LOCAL_PORT = 50555


def parse_args():
    """Parse the command line into the topology inputs"""
    parser = argparse.ArgumentParser(description="DoomCoprocessor F Prime deployment")
    parser.add_argument("-a", "--address", default=DEFAULT_REMOTE_ADDRESS,
                        help="Control node (DoomFlight) hub address (default %(default)s)")
    parser.add_argument("-p", "--port", type=int, default=DEFAULT_REMOTE_PORT,
                        help="Control node hub UDP port (default %(default)s)")
    parser.add_argument("-u", "--local-port", type=int, default=DEFAULT_LOCAL_PORT,
                        help="Local hub UDP port (default %(default)s)")
    parser.add_argument("-f", "--frame-sink", default=os.environ.get("DOOM_FRAME_SINK"),
                        help="host:port UDP sink the Python frameReader forwards frames and palettes to, for the "
                             "doom-satellite-view viewer (default $DOOM_FRAME_SINK, none when unset)")
    return parser.parse_args()


def fsw_main():
    """Set up, run, and tear down the DoomCoprocessor topology"""
    args = parse_args()
    fprime_py.Os.init()
    state = fprime_py.TopologyState()
    state.hubRemoteAddress = args.address
    state.hubRemotePort = args.port
    state.hubLocalPort = args.local_port
    # The Python frameReader instance is constructed inside setup_topology and reads its sink from the environment
    if args.frame_sink:
        os.environ["DOOM_FRAME_SINK"] = args.frame_sink

    print("Hit Ctrl-C to quit", flush=True)
    fprime_py.setup_topology(state)
    # Blocks in the timer driving the rate groups; SIGINT/SIGTERM stop it
    fprime_py.run_topology()
    fprime_py.teardown_topology(state)
    print("Exiting...", flush=True)


if __name__ == "__main__":
    fsw_main()

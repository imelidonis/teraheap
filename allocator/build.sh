#!/usr/bin/env bash

# Define the "error" values
ERRORS[INVALID_OPTION]=1
ERRORS[INVALID_ARG]=2
ERRORS[OUT_OF_RANGE]=3
ERRORS[NOT_AN_INTEGER]=4
ERRORS[PROGRAMMING_ERROR]=5

DEBUG=0

# Function to display usage message
function usage() {
  echo "Usage: $0 [options]"
  echo "Options:"
  echo "  -d, --debug                           Build allocator with debug symbols."
  echo "  -h, --help                            Display this help message and exit."
  echo
  echo "   Examples:"
  echo
  echo "  ./build.sh                            Build allocator without debug symbols."
  echo "  ./build.sh -d                         Build allocator with debug symbols."
  echo "  ./build.sh --debug                    Build allocator with debug symbols."

  return 0 2>/dev/null || exit 0
}

function parse_script_arguments() {
  local OPTIONS=dh
  local LONGOPTIONS=debug,help

  # Use getopt to parse the options
  local PARSED=$(getopt --options=$OPTIONS --longoptions=$LONGOPTIONS --name "$0" -- "$@")

  # Check for errors in getopt
  if [[ $? -ne 0 ]]; then
    return ${ERRORS[INVALID_OPTION]} 2>/dev/null || exit ${ERRORS[INVALID_OPTION]}
  fi

  # Evaluate the parsed options
  eval set -- "$PARSED"

  while true; do
    case "$1" in
    -d | --debug)
      DEBUG=1
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      echo "Programming error"
return ${ERRORS[PROGRAMMING_ERROR]} 2>/dev/null || exit ${ERRORS[PROGRAMMING_ERROR]} # This will return if sourced, and exit if run as a standalone script
      ;;
    esac
  done
}

parse_script_arguments "$@"

# Export the DEBUG variable to be used in the Makefile
export DEBUG

make clean
make distclean
make lib


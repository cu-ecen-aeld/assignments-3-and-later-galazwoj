#!/bin/sh

if [ $# -ne 1 ]; then 
	echo "Parameter missing"
	exit 1
fi

valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind-out.txt $1 

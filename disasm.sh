#!/bin/bash
for f in *.obj
do
	wdis "$f" -s -l -p > "$(basename -s .obj "$f").lst"
done

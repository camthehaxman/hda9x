#!/bin/bash
for f in *.obj
do
	wdis "$f" -s > "$(basename -s .obj "$f").lst"
done

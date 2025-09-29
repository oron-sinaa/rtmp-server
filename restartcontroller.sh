#!/bin/bash
echo -e "\x1b[31m[RTMPServer] [Exiting] Restarting RTMPServer...\x1b[0m"
kill -9 $(ps aux | grep 'MistController' | grep -v grep | sort -n -k 2 | awk '{print $2}' | head -n 1)
exit 1

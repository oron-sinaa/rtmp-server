#!/bin/bash

ENTRYLABEL=${ENTRYLABEL}

if [[ "$ENTRYLABEL" = "RTMPServer" ]]; then
	if ( test -f "/etc/rtmpserver/config.json" )
	then
			echo -e "\n\033[33mLaunching supervisor in non-daemon mode...\033[0m"
			echo -e "\033[33m-------------------------------------------------\033[0m"
			echo -e "\033[94mR T M P S e r v e r  is now booting up...\033[0m"
			echo -e "\033[94m-------------------------------------------------\033[0m"
			exec /usr/bin/supervisord -c /etc/supervisor/conf.d/supervisord.conf
	else
			echo -e "\033[31mMissing config.json for RTMPServer! Please bind it to /etc/rtmpserver"
	fi;
elif [[ "$ENTRYLABEL" = "Debug" ]]; then
	sleep infinity
else
	echo -e "\033[31mNO VALID ENTRYPOINT CHOSEN. Can be 'RTMPServer'. Exiting...\033[0m"
fi;

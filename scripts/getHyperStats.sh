#!/bin/sh
HOSTNAME=$(hostname -A)

#TODO: split out units
#TODO: fix datetime layout

echo "datetime,fqdn,tag,measure"

printf "%s\n" hosttime speed sessionid balloon swap memlimit memres cpures cpulimit | xargs -n 1 -I '{}' sh -c 'prefix=$(echo "{}"| sed "s/^[ \t]*//" | sed "s/[ \t]*$//" | sed "s/ /./"); vmware-toolbox-cmd stat {} | awk -v dt=$(date +\%d/\%m/\%YT\%H:\%M:\%S\%Z) -v hst=$HOSTNAME  -v pfx=$prefix "function ltrim(s) { sub(/^[ \t\r\n]+/, \"\", s); return s } function rtrim(s) { sub(/[ \t\r\n]+$/, \"\", s); return s } function trim(s)  { return rtrim(ltrim(s)); } BEGIN{FS=\"=\"} { if(NF==2) {print dt\",\"hst\",\"pfx\".\"trim(\$1)\",\"trim(\$2)} if(NF==1)  {print dt\",\"hst\",\"pfx\",\"trim(\$1)} } " '

vmware-toolbox-cmd stat raw | xargs -n 1 -I '{}' sh -c 'prefix=$(echo "{}"| sed "s/^[ \t]*//" | sed "s/[ \t]*$//" | sed "s/ /./"); vmware-toolbox-cmd stat raw text {} | awk -v dt=$(date +\%d/\%m/\%YT\%H:\%M:\%S\%Z) -v hst=$HOSTNAME -v pfx=$prefix "function ltrim(s) { sub(/^[ \t\r\n]+/, \"\", s); return s } function rtrim(s) { sub(/[ \t\r\n]+$/, \"\", s); return s } function trim(s)  { return rtrim(ltrim(s)); } BEGIN{FS=\"=\"} {print dt\",\"hst\",\"pfx\".\"trim(\$1)\",\"trim(\$2)}" '

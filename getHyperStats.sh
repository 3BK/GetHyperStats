#!/bin/sh

echo "datetime,fqdn,tag,measure"
vmware-toolbox-cmd stat raw | xargs -n 1 -I '{}' sh -c 'prefix=$(echo "{}"| sed "s/^[ \t]*//" | sed "s/[ \t]*$//" | sed "s/ /./"); vmware-toolbox-cmd stat raw text {} | awk -v dt=$(date +\%d/\%m/\%YT\%H:\%M:\%S\%Z) -v hst=$(hostname -A)  -v pfx=$prefix "function ltrim(s) { sub(/^[ \t\r\n]+/, \"\", s); return s } function rtrim(s) { sub(/[ \t\r\n]+$/, \"\", s); return s } function trim(s)  { return rtrim(ltrim(s)); } BEGIN{FS=\"=\"} {print dt\",\"hst\",\"pfx\".\"trim(\$1)\",\"trim(\$2)}" '

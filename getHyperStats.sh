#!/bin/sh

vmware-toolbox-cmd stat raw | xargs -n 1 -I '{}' sh -c 'prefix=$(echo "{}"| sed "s/^[ \t]*//" | sed "s/[ \t]*$//" | sed "s/ /./"); vmware-toolbox-cmd stat raw text {} | awk -v dt=$(date +\%d/\%m/\%YT\%H:\%M:\%S\%Z) -v hst=$(hostname -A)  -v pfx=$prefix "BEGIN{FS=\"=\"} {print dt\",\"hst\",\"pfx\".\"\$1\",\"\$2}" '

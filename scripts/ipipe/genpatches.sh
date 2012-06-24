#! /bin/sh

VERSION=`sed 's/^VERSION = \(.*\)/\1/;t;d' Makefile`
PATCHLEVEL=`sed 's/^PATCHLEVEL = \(.*\)/\1/;t;d' Makefile`
SUBLEVEL=`sed 's/^SUBLEVEL = \(.*\)/\1/;t;d' Makefile`
EXTRAVERSION=`sed 's/^EXTRAVERSION = \(.*\)/\1/;t;d' Makefile`

if [ -z "$SUBLEVEL" ]; then
    kvers="$VERSION.$PATCHLEVEL"
elif [ -z "$EXTRAVERSION" ]; then
    kvers="$VERSION.$PATCHLEVEL.$SUBLEVEL"
else
    kvers="$VERSION.$PATCHLEVEL.$SUBLEVEL.$EXTRAVERSION"
fi

echo kernel version: $kvers

git diff "v$kvers" | awk -v kvers="$kvers" \
'function set_current_arch(a)
{
    if (!outfiles[a]) {
	mt = "mktemp /tmp/XXXXXX"
	mt | getline outfiles[a]
	close(mt)
    }
    current_arch=a
    current_file=outfiles[a]
}

match($0, /^diff --git a\/arch\/([^[:blank:]\/]*)/, arch) {
    a=arch[1]

    set_current_arch(a)
    print $0 >> current_file
    next
}

match($0, /^diff --git a\/drivers\/([^[:blank:]]*)/, file) {
    f=file[1]

    switch(f) {
    case /clocksource\/i8253.c|pci\/htirq.c/:
	 a="x86"
	 break

    case /gpio\/gpio-mxc.c|gpio\/gpio-omap.c|gpio\/gpio-pxa.c|gpio\/gpio-sa1100.c|mfd\/twl6030-irq.c|misc\/Kconfig/:
	 a="arm"
	 break

    case /tty\/serial\/8250.c/:
	 a="noarch"
	 break

    case /tty\/serial\/bfin_uart.c/:
	 a="blackfin"
	 break

    case /tty\/serial\/mpc52xx_uart.c|gpio\/gpio-mpc8xxx.c/:
	 a="powerpc"
	 break

    default:
	 print "Error unknown architecture for driver "f
	 exit 1
    }

    set_current_arch(a)
    print $0 >> current_file
    next
}

/^diff --git a\/scripts\/ipipe\/genpatches.sh/ {
    current_file="/dev/null"
    current_arch="nullarch"
    next
}

/^diff --git/ {
    set_current_arch("noarch")
    print $0 >> current_file
    next
}

match ($0, /#define [I]PIPE_CORE_RELEASE[[:blank:]]*([^[:blank:]]*)/, vers) {
    version[current_arch]=vers[1]
}

{
    print $0 >> current_file
}

END {
    close(outfiles["noarch"])
    for (a in outfiles)
	if (a != "noarch") {
	    dest="ipipe-core-"kvers"-"a"-"version[a]".patch"
	    close(outfiles[a])
	    system("mv "outfiles[a]" "dest)
	    system("cat "outfiles["noarch"]" >> "dest)
	    print dest
	}

    system("rm "outfiles["noarch"])
}
'

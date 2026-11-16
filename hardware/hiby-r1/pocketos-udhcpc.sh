#!/bin/sh
# DHCP data is passed as arguments, never evaluated as shell commands.
set -eu
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
[ "${interface:-}" = wlan0 ] || exit 2
ipv4(){
    printf '%s\n' "$1" | awk -F. 'NF!=4 {exit 1} {for(i=1;i<=4;i++)if($i!~/^[0-9]+$/ || $i>255)exit 1}'
}
case "${1:-}" in
    deconfig|nak)
        ip addr flush dev wlan0 scope global
        ip route flush dev wlan0 || :
        ;;
    bound|renew)
        ipv4 "${ip:-}" && ipv4 "${subnet:-}" || exit 1
        # ifconfig accepts a dotted mask; subsequent route arguments are each
        # one validated IPv4 address, even on a malicious DHCP server.
        ip route flush dev wlan0 || :
        ifconfig wlan0 "$ip" netmask "$subnet" up
        for gateway in ${router:-}; do
            ipv4 "$gateway" || exit 1
            ip route add default via "$gateway" dev wlan0
            break
        done
        umask 077
        tmp=$(mktemp /tmp/pocketos-resolv.XXXXXX)
        trap 'rm -f "$tmp"' EXIT
        for server in ${dns:-}; do ipv4 "$server" || exit 1; printf 'nameserver %s\n' "$server" >> "$tmp"; done
        mv -f "$tmp" /tmp/resolv.conf
        printf '%s\n' "$ip" > /run/pocketos-wifi/ipv4
        ;;
    *) exit 0;;
esac

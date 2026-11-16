#!/usr/bin/env python3
"""Adapt the pinned Android daemon for the R1's FunctionFS USB interface."""
import argparse
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('source', type=Path)
p.add_argument('functionfs_source', type=Path)
a = p.parse_args()
usb = a.functionfs_source.read_text()
old = '#define cpu_to_le16(x)  htole16(x)\n#define cpu_to_le32(x)  htole32(x)'
new = '''#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define cpu_to_le16(x) ((__le16)(x))
#define cpu_to_le32(x) ((__le32)(x))
#else
#define cpu_to_le16(x) ((__le16)__builtin_bswap16(x))
#define cpu_to_le32(x) ((__le32)__builtin_bswap32(x))
#endif'''
if usb.count(old) != 1:
    raise SystemExit('Unexpected pinned FunctionFS source')
(a.source/'core/adbd/usb_linux_client.c').write_text(usb.replace(old, new))
path = a.source/'core/adbd/adb.c'
s = path.read_text()
old = '''    if (!usb) {
        printf("Using USB\\n");
        // listen on default port
        local_init(DEFAULT_ADB_LOCAL_TRANSPORT_PORT);
    }'''
new = '''    if (!usb) {
#ifdef POCKETOS_ADBD_TCP_TEST
        local_init(18557);
#else
        fprintf(stderr, "PocketOS adbd: USB endpoints unavailable; TCP fallback disabled.\\n");
        return 1;
#endif
    }'''
if s.count(old) != 1:
    raise SystemExit('Unexpected pinned daemon USB fallback')
s = s.replace(old, new)
marker = '''    } else {
        char local_name[30];
        build_local_name(local_name, sizeof(local_name), server_port);
        if(install_listener(local_name, "*smartsocket*", NULL, 0)) {
            exit(1);
        }
    }

    int usb = 0;'''
replacement = '''    } else {
#ifdef POCKETOS_ADBD_TCP_TEST
        char local_name[30];
        build_local_name(local_name, sizeof(local_name), 18558);
        if(install_listener(local_name, "*smartsocket*", NULL, 0)) exit(1);
#endif
    }

    int usb = 0;'''
if s.count(marker) != 1:
    raise SystemExit('Unexpected pinned local daemon listener')
s = s.replace(marker, replacement)
marker = '''int main(int argc, char **argv)
{'''
replacement = '''int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--pocketos-build-info")) {
        puts("PocketOS adbd 4.2.2 / AOSP FunctionFS 5.1.1 / USB-only");
        return 0;
    }'''
if s.count(marker) != 1:
    raise SystemExit('Unexpected daemon entry point')
path.write_text(s.replace(marker, replacement))
path = a.source/'core/adbd/transport_local.c'
s = path.read_text()
old = 'serverfd = socket_inaddr_any_server(port, SOCK_STREAM);'
if s.count(old) != 1:
    raise SystemExit('Unexpected TCP test listener')
path.write_text(s.replace(old, '''#ifdef POCKETOS_ADBD_TCP_TEST
            serverfd = socket_loopback_server(port, SOCK_STREAM);
#else
            serverfd = socket_inaddr_any_server(port, SOCK_STREAM);
#endif'''))

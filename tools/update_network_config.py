#!/usr/bin/env python3
"""Update only the three network limits in an existing ESP-IDF sdkconfig."""
from pathlib import Path
config = Path(__file__).resolve().parents[1] / 'sdkconfig'
values = {'CONFIG_LWIP_MAX_SOCKETS': '20', 'CONFIG_LWIP_DHCPS_MAX_STATION_NUM': '12',
          'CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM': '2'}
if config.exists():
    lines = config.read_text().splitlines()
    pending = values.copy()
    for i, line in enumerate(lines):
        key = line.split('=', 1)[0]
        if key in pending:
            lines[i] = f'{key}={pending.pop(key)}'
    lines.extend(f'{key}={value}' for key, value in pending.items())
    config.write_text('\n'.join(lines) + '\n')
    print('Updated WiFi/socket/DHCP limits; all other sdkconfig settings preserved.')
else:
    print('No sdkconfig yet; sdkconfig.defaults will supply the network limits on first build.')

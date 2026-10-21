# SailRSS

A native [FreshRSS](https://freshrss.org/) client for Sailfish OS.

**Status:** early development. The FreshRSS API client, local database and
sync are done and tested; the app does not use them yet. See [docs/PLAN.md](docs/PLAN.md) for the roadmap.

## Requirements

- [Sailfish SDK](https://docs.sailfishos.org/Tools/Sailfish_SDK/) 3.13 or
  newer, with the Docker build engine and the `SailfishOS-5.1.0.11-aarch64`
  target
- The project checked out inside the SDK workspace (by default your home
  directory, configurable in the SDK)
- A Sailfish OS device registered in the SDK (Qt Creator: Options → Devices)
- A FreshRSS server with API access enabled (Administration → Authentication)
  and an API password set in your user profile

## Building and deploying

```sh
scripts/dev.sh build     # build the RPM into build/aarch64/RPMS/, runs unit tests
scripts/dev.sh check     # run the Jolla Store (Harbour) validator
scripts/dev.sh deploy    # install on the device (confirm on the phone)
scripts/dev.sh all       # all three
```

The script defaults to the `SailfishOS-5.1.0.11-aarch64` target and the device
named `Jolla Phone (2026) (ARM 64bit)`. Override them with the `SFDK_TARGET`
and `SFDK_DEVICE` environment variables, and the `sfdk` path with `SFDK`.

After changing user-visible strings, run `scripts/dev.sh lupdate` to refresh
the translation files in `translations/`.

## Testing

`scripts/dev.sh build` runs the unit tests in `tests/` under emulation;
`scripts/dev.sh test` reruns them.

`scripts/dev.sh live-test` runs the API client against a real FreshRSS
server. It reads the server and credentials from environment variables
(development only; this will be replaced by proper credential storage):

```sh
export SAILRSS_URL=https://rss.example.net SAILRSS_USER=me SAILRSS_PASSWORD=api-password
scripts/dev.sh live-test
```

Read-only checks always run. `SAILRSS_LIVE_WRITE=1` also tests marking
articles read and starred; the test restores the original state.

For a throwaway local server, run FreshRSS in Podman. The tests run inside
the SDK's Docker network, so FreshRSS must also listen on that network's
gateway (`docker network inspect sailfish-sdk:$USER`, here `172.18.0.1`):

```sh
podman run -d --name sailrss-freshrss \
  -p 127.0.0.1:8089:80 -p 172.18.0.1:8089:80 \
  -e FRESHRSS_INSTALL='--api-enabled --default-user tester' \
  -e FRESHRSS_USER='--user tester --password webtest123 --api-password apitest123 --email tester@example.net' \
  docker.io/freshrss/freshrss:1.30.3

SAILRSS_URL=http://172.18.0.1:8089 SAILRSS_USER=tester SAILRSS_PASSWORD=apitest123 \
  SAILRSS_LIVE_WRITE=1 scripts/dev.sh live-test
```

## License

[GPL-3.0-or-later](LICENSE)
